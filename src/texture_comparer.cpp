#include "texture_comparer.hpp"

#include "kwin_compat.hpp"

#include "blur_cache.hpp"

#include <effect/effectwindow.h>
#include <opengl/glshadermanager.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QLoggingCategory>
#include <QtPreprocessorSupport>
#include <QFile>

#include <epoxy/gl.h>

#include <memory>
#include <unordered_map>
#include <array>

Q_LOGGING_CATEGORY(BBDX_TEXTURE_COMPARER, "kwin_effect_better_blur_dx.texture_comparer", QtInfoMsg)

/**
 * These are the formats in KWin's src/core/drm_formats.cpp
 * so I guess these are all possible values?
 *
 * returns a pair of the normalized enum and glsls format string
 * 
 * Fallback to rgba8 in case we encounter a weird format
 */
static inline std::pair<GLenum, const char*> glslFormat(GLenum internalFormat) {
    switch (internalFormat) {
        case GL_R8:         return {internalFormat, "r8"};
        case GL_R16:        return {internalFormat, "r16"};
        case GL_RGBA8:      return {internalFormat, "rgba8"};
        case GL_RGB10_A2:   return {internalFormat, "rgb10_a2"};
        case GL_RGBA16:     return {internalFormat, "rgba16"};
        case GL_RGBA16F:    return {internalFormat, "rgba16f"};
        
        // unknown or format with no glsl equivalent
        case GL_RGB5_A1:
        case GL_RGBA4:
        default:
            qCWarning(BBDX_TEXTURE_COMPARER) << "Unhandled texture format:"
                                             << internalFormat
                                             << "- falling back to rgba8";
            return {GL_RGBA8, "rgba8"};
    }
}

/**
 * Map dirtyRegion into backgroundRect (texture local coords)
 * and flip along the Y axis.
 *
 * returns pair of dirtyRegion as std::vector<ComputeShaderRect> and its boundingRect
 */
static inline std::pair<std::vector<BBDX::TextureComparer::ComputeShaderRect>, KWin::Rect> localDirtyRegionGL(const KWin::Region &dirtyRegion, const KWin::Rect &backgroundRect) {
    std::vector<BBDX::TextureComparer::ComputeShaderRect> glDirtyRegion{};
    glDirtyRegion.reserve(dirtyRegion.rects().size());

    const auto localDirtyRegion = dirtyRegion.translated(-backgroundRect.topLeft());

    for (const auto &rect : localDirtyRegion.rects()) {
        // ignore empty rects
        if (rect.width() <= 0 || rect.height() <= 0) {
            continue;
        }

        const int glY = backgroundRect.height() - (rect.y() + rect.height());
        glDirtyRegion.emplace_back(rect.x(), glY, rect.width(), rect.height());
    }

    KWin::Rect boundingRect;
    {
        const auto &rect = localDirtyRegion.boundingRect();
        const int glY = backgroundRect.height() - (rect.y() + rect.height());
        boundingRect = KWin::Rect{rect.x(), glY, rect.width(), rect.height()};
    }

    return {std::move(glDirtyRegion), std::move(boundingRect)};
}

std::unique_ptr<BBDX::TextureComparer::WindowData> BBDX::TextureComparer::WindowData::create() {
    std::unique_ptr<WindowData> windowData{new WindowData{}};

    glGenBuffers(SLOTS, windowData->m_counterBuffers.data());
    glGenQueries(SLOTS, windowData->m_queries.data());

    // allocate a single GLuint (the change counter)
    for (const auto &buffer : windowData->m_counterBuffers) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // spin up a dummy query on all query objects so they
    // have a defined state
    for (const auto &query : windowData->m_queries) {
        glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
        glEndQuery(GL_ANY_SAMPLES_PASSED);
    }

    return windowData;
}

BBDX::TextureComparer::WindowData::~WindowData() {
    glDeleteBuffers(SLOTS, m_counterBuffers.data());
    glDeleteQueries(SLOTS, m_queries.data());
}


std::optional<std::pair<GLuint, GLuint>> BBDX::TextureComparer::WindowData::getSlot() {
    int slot{m_lastSlot};
    if (++slot >= SLOTS) {
        slot = 0;
    }

    while (slot != m_lastSlot) {
        GLuint result{GL_FALSE};
        glGetQueryObjectuiv(m_queries[slot], GL_QUERY_RESULT_AVAILABLE, &result);
        if (result == GL_TRUE) {
            m_lastSlot = slot;
            return {{m_counterBuffers[slot], m_queries[slot]}};
        }

        if (++slot >= SLOTS) {
            slot = 0;
        }
    }

    return std::nullopt;
}

std::unique_ptr<BBDX::TextureComparer::ComputeShader> BBDX::TextureComparer::buildComputeShader(GLenum textureFormat) {
    const auto [normalizedFormat, glslString] = glslFormat(textureFormat);

    qCDebug(BBDX_TEXTURE_COMPARER) << "Creating texture compare instance for" << glslString;

    // we need to handle the compute shader manually
    QFile shaderFile{QStringLiteral(":/effects/better_blur_dx/shaders/texture_compare_and_update.comp")};
    if (!shaderFile.open(QIODevice::ReadOnly)) {
        qCWarning(BBDX_TEXTURE_COMPARER) << "Failed to open tecture compare compute shader";
        return nullptr;
    }
    QByteArray shaderSource = shaderFile.readAll();

    QByteArray formatMacro = QByteArray("#define TEXTURE_FORMAT ") + glslString + "\n";

    // we assume the very first line has the #version
    // inject our TEXTURE_FORMAT macro after that
    int secondLine = shaderSource.indexOf("\n");
    if (secondLine > -1) {
        shaderSource.insert(secondLine + 1, formatMacro);
    } else {
        qCWarning(BBDX_TEXTURE_COMPARER) << "Compute shader template appears to be incomplete";
        shaderSource.prepend(formatMacro);
    }

    // this process roughly mirrors what KWin does in
    // GLShader::compile()
    // minus all the preprocessing magic which is vert/frag specific

    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);

    const GLchar *src = shaderSource.constData();
    const GLint srcLength = shaderSource.length();
    glShaderSource(shader, 1, &src, &srcLength);

    // compile
    glCompileShader(shader);

    // configure log buffer
    GLint maxLength, length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
    QByteArray log(maxLength, 0);
    glGetShaderInfoLog(shader, maxLength, &length, log.data());

    // compile status
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

    if (status == 0) {
        qCCritical(BBDX_TEXTURE_COMPARER) << "Compute shader compilation failed:\n" << log;
        glDeleteShader(shader);
        return nullptr;
    } else {
        qCDebug(BBDX_TEXTURE_COMPARER) << "Compute shader compilation log:\n" << log;
    }

    auto computeShader = std::make_unique<ComputeShader>();
    computeShader->program = glCreateProgram();
    glAttachShader(computeShader->program, shader);
    glDeleteShader(shader);

    // now link the program similar to GLShader::link()
    glLinkProgram(computeShader->program);

    // reconfigure log buffer
    glGetProgramiv(computeShader->program, GL_INFO_LOG_LENGTH, &maxLength);
    log = QByteArray{maxLength, 0};
    glGetProgramInfoLog(computeShader->program, maxLength, &length, log.data());

    // link status
    glGetProgramiv(computeShader->program, GL_LINK_STATUS, &status);

    if (status == 0) {
        qCCritical(BBDX_TEXTURE_COMPARER) << "Compute shader linking failed:\n" << log;
        // ~ComputeShader() handles glDeleteProgram
        return nullptr;
    } else {
        qCDebug(BBDX_TEXTURE_COMPARER) << "Compute shader linking log:\n" << log;
    }

    // dirtyRegion buffer
    glGenBuffers(1, &computeShader->dirtyRegionBuffer);

    // store locations of uniform params
    computeShader->dirtyRegionRectCountLocation = glGetUniformLocation(computeShader->program, "u_dirtyRegionRectCount");
    computeShader->dirtyRegionBoundingBoxLocation = glGetUniformLocation(computeShader->program, "u_dirtyRegionBoundingBox");

    return computeShader;
}

std::unique_ptr<BBDX::TextureComparer> BBDX::TextureComparer::create() {
    auto textureComparer = std::unique_ptr<TextureComparer>(new TextureComparer());

    // for vert+frag just use the KWin helpers
    textureComparer->m_glueShader = KWin::ShaderManager::instance()->generateShaderFromFile(KWin::ShaderTraits{},
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/texture_compare_and_update.vert"),
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/texture_compare_and_update.frag"));
    if (!textureComparer->m_glueShader) {
        qCWarning(BBDX_TEXTURE_COMPARER) << "Failed to load texture compare glue shaders";
        return nullptr;
    }

    // most things are GL_RGBA8 so we'll create that upfront
    // (and use it as a sanity check for compile issues)
    auto computeShader = buildComputeShader(GL_RGBA8);
    if (!computeShader) {
        qCWarning(BBDX_TEXTURE_COMPARER) << "Failed to load texture compare compute shader";
        return nullptr;
    }
    textureComparer->m_computeShaders.emplace(GL_RGBA8, std::move(computeShader));

    return textureComparer;
}

static inline bool drainGlErrors(const char *context) {
    bool sawError = false;
    GLenum err;
    int guarded = 0;
    while ((err = glGetError()) != GL_NO_ERROR && guarded++ < 8) {
        if (!sawError) {
            qCWarning(BBDX_TEXTURE_COMPARER) << "GL error during" << context << ":";
        }
        sawError = true;
        const char *name = "UNKNOWN";
        switch (err) {
            case GL_INVALID_ENUM:                 name = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE:                name = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION:            name = "GL_INVALID_OPERATION"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION:name = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            case GL_OUT_OF_MEMORY:                name = "GL_OUT_OF_MEMORY"; break;
            case 0x0507:                          name = "GL_CONTEXT_LOST"; break;
        }
        qCWarning(BBDX_TEXTURE_COMPARER) << "  " << name << "(0x" << Qt::hex << err << Qt::dec << ")";
    }
    return sawError;
}

// GL_READ_WRITE requires immutable storage (glTexStorage*).
// Fall back to GL_READ_ONLY if not immutable so the bind doesn't
// silently fail and leave the image unit unbound.
static inline bool bindImageTextureSafe(int unit, KWin::GLTexture *tex, GLenum accessMode, GLenum internalFormat) {
    if (accessMode != GL_READ_ONLY) {
        tex->bind();
        GLint immutable = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_IMMUTABLE_FORMAT, &immutable);
        if (immutable == 0) {
            accessMode = GL_READ_ONLY;
        }
    }
    while (glGetError() != GL_NO_ERROR) { }
    glBindImageTexture(unit, tex->texture(), 0, GL_FALSE, 0, accessMode, internalFormat);
    return !drainGlErrors("glBindImageTexture");
}

bool BBDX::TextureComparer::compareAndUpdate(const std::pair<GLuint, GLuint> &windowDataSlot, KWin::GLTexture *freshBlit, KWin::GLTexture *cachedBlit, const BBDX::BlurCachePaintData &paintData) {
    const GLuint counterBuffer{windowDataSlot.first};
    const GLuint query{windowDataSlot.second};

    const auto [normalizedFormat, glslString] = glslFormat(freshBlit->internalFormat());

    // lazily create compute shader instances in case we need
    // them for non GL_RGBA8
    if (!m_computeShaders.contains(normalizedFormat)) {
        // TODO:
        // it's probably bad if this fails... I'll probably deal with
        // it later.. maybe...
        auto newComputeShader = buildComputeShader(normalizedFormat);
        if (!newComputeShader) {
            qCWarning(BBDX_TEXTURE_COMPARER) << "Failed to load texture compare compute shader";
            return false;
        }
        m_computeShaders.emplace(normalizedFormat, std::move(newComputeShader));
    }

    ComputeShader *computeShader{m_computeShaders.at(normalizedFormat).get()};

    // drain stale errors so our own checks below are accurate
    while (glGetError() != GL_NO_ERROR) { }

    // ensure blitted texture is complete
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // bind the textures
    if (!bindImageTextureSafe(0, freshBlit, GL_READ_ONLY, normalizedFormat) ||
        !bindImageTextureSafe(1, cachedBlit, GL_READ_WRITE, normalizedFormat)) {
        return false;
    }

    // reset and bind counter
    const GLuint zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterBuffer);
    glClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, 0, sizeof(GLuint), GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    // slot 2 - matching compute shader
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, counterBuffer);

    // prepare dirtyRegion
    const auto [glDirtyRegion, boundingRect] = localDirtyRegionGL(*paintData.dirtyRegion, *paintData.backgroundRect);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, computeShader->dirtyRegionBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, glDirtyRegion.size() * sizeof(ComputeShaderRect), glDirtyRegion.data(), GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, computeShader->dirtyRegionBuffer);

    // prepare compute shader
    GLint prevProgram{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glUseProgram(computeShader->program);

    glUniform1i(computeShader->dirtyRegionRectCountLocation, glDirtyRegion.size());
    glUniform4i(computeShader->dirtyRegionBoundingBoxLocation, boundingRect.x(), boundingRect.y(), boundingRect.width(), boundingRect.height());

    // dispatch in 16x16 workgroup blocks (ceiled, matching compute shader params)
    if (boundingRect.width() > 0 && boundingRect.height() > 0) {
        glDispatchCompute((boundingRect.width() + 15) / 16, (boundingRect.height() + 15) / 16, 1);
    }

#if defined(BBDX_DEBUG)
    // in debug builds log the changed pixels
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counterBuffer);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    GLuint pixelsChanged{0};
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &pixelsChanged);
    if (pixelsChanged > 0) {
        if (paintData.window) {
            qCDebug(BBDX_TEXTURE_COMPARER) << "Pixels changed (" << paintData.window->windowClass() << "):" << pixelsChanged;
        } else {
            qCDebug(BBDX_TEXTURE_COMPARER) << "Pixels changed:" << pixelsChanged;
        }
        qCDebug(BBDX_TEXTURE_COMPARER) << "In boundingRect:" << boundingRect;
    }
#endif

    // ensure SSBO is flushed for query
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);

    // done with the textures and dirtyRegion, counterBuffer is still needed by query
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
    glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);

    // revert to whatever program was used before
    // (let's hope this doesn't mess up KWin state)
    glUseProgram(prevProgram);

    KWin::ShaderManager::instance()->pushShader(m_glueShader.get()); 

    // "draw" a single point to check the result of
    // the compute shader
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);

    while (glGetError() != GL_NO_ERROR) { }
    glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
    glDrawArrays(GL_POINTS, 0, 1);
    glEndQuery(GL_ANY_SAMPLES_PASSED);
    const bool queryFailed = drainGlErrors("glBeginQuery/glDrawArrays/glEndQuery");

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    KWin::ShaderManager::instance()->popShader();

    // cleanup
    // unbind counterBuffer in reverse order
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return !queryFailed;
}
