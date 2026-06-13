#include "texture_comparer.hpp"

#include "kwin_compat.hpp"

#include <effect/effectwindow.h>
#include <opengl/glshadermanager.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QLoggingCategory>
#include <QFile>

#include <epoxy/gl.h>

#include <memory>
#include <qloggingcategory.h>
#include <unordered_map>

Q_LOGGING_CATEGORY(BBDX_TEXTURE_COMPARER, "kwin_effect_better_blur_dx.texture_comparer", QtInfoMsg)

/**
 * These are the formats in KWin's src/core/drm_formats.cpp
 * so I guess these are all possible values?
 * 
 * Fallback to rgba8 in case we encounter a weird format
 */
static inline const char* glslFormatString(GLenum internalFormat) {
    switch (internalFormat) {
        case GL_R8:         return "r8";
        case GL_R16:        return "r16";
        case GL_RGBA8:      return "rgba8";
        case GL_RGB10_A2:   return "rgb10_a2";
        case GL_RGBA16:     return "rgba16";
        case GL_RGBA16F:    return "rgba16f";
        
        // unknown or format with no glsl equivalent
        case GL_RGB5_A1:
        case GL_RGBA4:
        default:
            qCWarning(BBDX_TEXTURE_COMPARER) << "Unhandled texture format:"
                                             << internalFormat
                                             << "- falling back to rgba8";
            return "rgba8";
    }
}

std::unique_ptr<BBDX::TextureComparer::ComputeShader> BBDX::TextureComparer::buildComputeShader(GLenum textureFormat) {
    qCDebug(BBDX_TEXTURE_COMPARER) << "Creating texture compare instance for" << glslFormatString(textureFormat);

    // we need to handle the compute shader manually
    QFile shaderFile{QStringLiteral(":/effects/better_blur_dx/shaders/texture_compare_and_update.comp")};
    if (!shaderFile.open(QIODevice::ReadOnly)) {
        qCWarning(BBDX_TEXTURE_COMPARER) << "Failed to open tecture compare compute shader";
        return nullptr;
    }
    QByteArray shaderSource = shaderFile.readAll();

    const char *formatToken = glslFormatString(textureFormat);
    QByteArray formatMacro = QByteArray("#define TEXTURE_FORMAT ") + formatToken + "\n";

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

    // store locations of uniform params
    computeShader->dirtyRectLocation = glGetUniformLocation(computeShader->program, "u_dirtyRect");

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

    // supplementary resources
    glGenBuffers(1, &textureComparer->m_counterBuffer);

    // allocate a single GLuint (the change counter)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, textureComparer->m_counterBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glGenQueries(1, &textureComparer->m_glueQuery);

    return textureComparer;
}

BBDX::TextureComparer::~TextureComparer() {
    if (m_counterBuffer > 0) {
        glDeleteBuffers(1, &m_counterBuffer);
    }

    if (m_glueQuery > 0) {
        glDeleteQueries(1, &m_glueQuery);
    }
}

void BBDX::TextureComparer::compareAndUpdate(KWin::GLTexture *freshBlit, KWin::GLTexture *cachedBlit, const KWin::Region &localDirtyRegionGL, const KWin::EffectWindow *window) {
    const auto textureFormat = freshBlit->internalFormat();

    // lazily create compute shader instances in case we need
    // them for non GL_RGBA8
    if (!m_computeShaders.contains(textureFormat)) {
        // TODO:
        // it's probably bad if this fails... I'll probably deal with
        // it later.. maybe...
        auto newComputeShader = buildComputeShader(textureFormat);
        if (!newComputeShader) {
            qCWarning(BBDX_TEXTURE_COMPARER) << "Failed to load texture compare compute shader";
            return;
        }
        m_computeShaders.emplace(textureFormat, std::move(newComputeShader));
    }

    ComputeShader *computeShader{m_computeShaders.at(textureFormat).get()};

    // bind the textures
    // TODO: colorspace might be different
    glBindImageTexture(0, freshBlit->texture(), 0, GL_FALSE, 0, GL_READ_ONLY, textureFormat);
    glBindImageTexture(1, cachedBlit->texture(), 0, GL_FALSE, 0, GL_READ_WRITE, textureFormat);

    // reset and bind counter
    const GLuint zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_counterBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &zero);
    // slot 2 - matching compute shader
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_counterBuffer);

    // prepare compute shader
    GLint prevProgram{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glUseProgram(computeShader->program);

    for (const auto &rect : localDirtyRegionGL.rects()) {
        // bind dirtyRect, in OpenGL coords
        glUniform4i(computeShader->dirtyRectLocation, rect.x(), rect.y(), rect.width(), rect.height());

        // dispatch in 16x16 workgroup blocks (ceiled, matching compute shader params)
        glDispatchCompute((rect.width() + 15) / 16, (rect.height() + 15) / 16, 1);
    }

    // wait for compute to be done, then fire the query
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT
                    | GL_COMMAND_BARRIER_BIT
                    | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                    | GL_TEXTURE_FETCH_BARRIER_BIT);

#if defined(BBDX_DEBUG)
    // in debug builds log the changed pixels
    GLuint pixelsChanged{0};
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &pixelsChanged);
    if (window) {
        qCDebug(BBDX_TEXTURE_COMPARER) << "Pixels changed (" << window->windowClass() << "):" << pixelsChanged;
    } else {
        qCDebug(BBDX_TEXTURE_COMPARER) << "Pixels changed:" << pixelsChanged;
    }
#endif

    // done with the textures, counterBuffer is still needed by query
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
    glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

    // revert to whatever program was used before
    // (let's hope this doesn't mess up KWin state)
    glUseProgram(prevProgram);

    KWin::ShaderManager::instance()->pushShader(m_glueShader.get()); 

    // "draw" a single point to check the result of
    // the compute shader
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);

    glBeginQuery(GL_ANY_SAMPLES_PASSED, m_glueQuery);
    glDrawArrays(GL_POINTS, 0, 1);
    glEndQuery(GL_ANY_SAMPLES_PASSED);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    KWin::ShaderManager::instance()->popShader();

    // cleanup
    // unbind counterBuffer in reverse order
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
