#include "blur_cache.hpp"

#include "blur.h"
#include "kwin_version.hpp"
#include "utils.h"

#include <epoxy/gl.h>
#include <qloggingcategory.h>
#include <sys/types.h>

#include <core/renderviewport.h>
#include <opengl/eglcontext.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
#  include "kwin_compat_6_5.hpp"
#else
#  include <core/rect.h>
#endif

#include <QLoggingCategory>
#include <QVector2D>
#include <QtNumeric>

#include <memory>
#include <vector>

Q_LOGGING_CATEGORY(BLUR_CACHE, "kwin_effect_better_blur_dx.blur_cache", QtInfoMsg)

BBDX::BlurCacheEntry::BlurCacheEntry(const KWin::Rect &scaledBackgroundRect, GLenum textureFormat, KWin::GLFramebuffer *sourceBlitFramebuffer) {
    // allocate new cached texture + framebuffer
    glClearColor(0, 0, 0, 0);
    cachedTexture = KWin::GLTexture::allocate(textureFormat, scaledBackgroundRect.size());
    if (!cachedTexture) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to allocate an offscreen texture";
        return;
    }
    cachedTexture->setFilter(GL_LINEAR);
    cachedTexture->setWrapMode(GL_CLAMP_TO_EDGE);

    cachedFramebuffer = std::make_unique<KWin::GLFramebuffer>(cachedTexture.get());
    if (!cachedFramebuffer->valid()) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
        return;
    }
#if defined(BETTERBLUR_X11)
    auto *context = KWin::OpenGlContext::currentContext();
#else
    auto *context = KWin::EglContext::currentContext();
#endif
    context->pushFramebuffer(cachedFramebuffer.get());
    glClear(GL_COLOR_BUFFER_BIT);
    context->popFramebuffer();


    // clone the blitTexture from the given framebuffer
    KWin::GLTexture *sourceTexture = sourceBlitFramebuffer->colorAttachment();

    blitTexture = KWin::GLTexture::allocate(sourceTexture->internalFormat(), sourceTexture->size());
    if (!blitTexture) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to allocate an offscreen texture";
        return;
    }
    blitTexture->setFilter(GL_LINEAR);
    blitTexture->setWrapMode(GL_CLAMP_TO_EDGE);

    auto blitFramebuffer = std::make_unique<KWin::GLFramebuffer>(blitTexture.get());
    if (!blitFramebuffer->valid()) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
        return;
    }

    KWin::GLFramebuffer::pushFramebuffer(sourceBlitFramebuffer);
    blitFramebuffer->blitFromFramebuffer();
    KWin::GLFramebuffer::popFramebuffer();
}

void BBDX::BlurCacheLRU::reset() {
    m_next = 0;
    m_valid = nullptr;
}

const BBDX::BlurCacheEntry* BBDX::BlurCacheLRU::next() {
    // wrap and empty cases
    if (m_next >= m_entries.size()) {
        return nullptr;
    }

    if (m_valid) {
        return nullptr;
    }

    return m_entries[m_next++].get();
}

void BBDX::BlurCacheLRU::select() {
    if (m_entries.empty()) {
        qCCritical(BLUR_CACHE) << "BlurCacheLRU::select(): Called with no entries";
        return;
    }

    size_t idx;
    if (m_next == 0) {
        idx = 0;
    } else {
        idx = m_next - 1;
    }

    auto selected = m_entries[idx].get();

    m_next = 0;
    m_valid = selected;

    qCDebug(BLUR_CACHE) << "Selected BlurCacheEntry:" << idx;

    for (auto &entry : m_entries) {
        if (entry->priority < selected->priority) {
            entry->priority += 1;
        }
    }
    selected->priority = 0;
}

void BBDX::BlurCacheLRU::add(std::unique_ptr<BlurCacheEntry> entry) {
    m_entries.insert(m_entries.begin(), std::move(entry));

    m_entries[0]->priority = 0;
    m_next = 0;
    select();

    qCDebug(BLUR_CACHE) << "Added new BlurCacheEntry";

    for (size_t i = 1; i < m_entries.size(); i++) {
        m_entries[i]->priority += 1;
    }

    while (m_entries.size() > m_max) {
        for (auto it = m_entries.begin(); it != m_entries.end(); it++) {
            if ((*it)->priority >= m_max) {
                m_entries.erase(it);
                qCDebug(BLUR_CACHE) << "Dropped old BlurCacheEntry";
                break;
            }
        }
    }
}

void BBDX::BlurCacheLRU::clear() {
    m_entries.clear();
    reset();
}

bool BBDX::BlurCacheData::invalidate(QStringView reason) {
    QString windowClass;
    pid_t windowPID;
    if (w) [[likely]] {
        windowClass = w->windowClass();
        windowPID = w->pid();
    } else {
        windowClass = QStringLiteral("unknown window");
        windowPID = 0;
    }

    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX << "Cache invalidated:" << windowClass << "\n"
                        << "PID:" << windowPID << "\n"
                        << "Hits:"   << hits << "\n"
                        << "Reason:" << reason;

    lru.clear();

    return true;
}

BBDX::BlurCache::BlurCache() {
    m_textureComparePass.shader = KWin::ShaderManager::instance()->generateShaderFromFile(KWin::ShaderTrait::MapTexture,
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/vertex.vert"),
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/texture_compare.frag"));
    if (!m_textureComparePass.shader) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to load texture compare pass shader";
        return;
    } else {
        m_textureComparePass.mvpMatrixLocation = m_textureComparePass.shader->uniformLocation("modelViewProjectionMatrix");
        m_textureComparePass.texUnitOldLocation = m_textureComparePass.shader->uniformLocation("texUnitOld");
        m_textureComparePass.texUnitNewLocation = m_textureComparePass.shader->uniformLocation("texUnitNew");
        m_textureComparePass.halfpixelLocation = m_textureComparePass.shader->uniformLocation("halfpixel");
        m_textureComparePass.borderIgnore = m_textureComparePass.shader->uniformLocation("borderIgnore");
    }

    m_texturePass.shader = KWin::ShaderManager::instance()->generateShaderFromFile(KWin::ShaderTrait::MapTexture,
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/vertex.vert"),
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/texture.frag"));
    if (!m_texturePass.shader) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to load texture pass shader";
        return;
    } else {
        m_texturePass.mvpMatrixLocation = m_texturePass.shader->uniformLocation("modelViewProjectionMatrix");
    }
}

void BBDX::BlurCache::selectCacheEntry(KWin::BlurRenderData &renderInfo,
                                           qreal opacity,
                                           KWin::GLVertexBuffer *vbo) {
    auto &cacheData = renderInfo.cache;
    if (!cacheData.opacity.has_value() || !qFuzzyCompare(cacheData.opacity.value(), opacity)) {
        cacheData.opacity = opacity;
        cacheData.invalidate(QStringLiteral("Opacity changed"));
    }

    cacheData.lru.reset();
    while (auto cacheEntry = cacheData.lru.next()) {
        KWin::GLTexture *prevBlitTexture = cacheEntry->blitTexture.get();
        KWin::GLFramebuffer *blitFramebuffer = renderInfo.framebuffers[0].get();
        KWin::GLTexture *blitTexture = blitFramebuffer->colorAttachment();

        // previous blit texture is definitely different
        if (!prevBlitTexture) {
            continue;
        }
        if (prevBlitTexture->size() != blitTexture->size()) {
            continue;
        }
        if (prevBlitTexture->internalFormat() != blitTexture->internalFormat()) {
            continue;
        }

        // fast path in case we already determined we
        // can't perform texture comparison
        if (!m_glQueryAvailable) [[unlikely]] {
            continue;
        }

        // check if textures differ on the pixel level
        // we'll just (ab)use the provided framebuffer for this
        // as it *should* always be correct
        KWin::ShaderManager::instance()->pushShader(m_textureComparePass.shader.get());
        KWin::GLFramebuffer::pushFramebuffer(blitFramebuffer);

        QMatrix4x4 projectionMatrix;
        projectionMatrix.ortho(QRectF(0.0, 0.0, blitTexture->width(), blitTexture->height()));

        m_textureComparePass.shader->setUniform(m_textureComparePass.mvpMatrixLocation, projectionMatrix);

        m_textureComparePass.shader->setUniform(m_textureComparePass.texUnitOldLocation, 0);
        glActiveTexture(GL_TEXTURE0);
        prevBlitTexture->bind();

        m_textureComparePass.shader->setUniform(m_textureComparePass.texUnitNewLocation, 1);
        glActiveTexture(GL_TEXTURE1);
        blitTexture->bind();

        m_textureComparePass.shader->setUniform(m_textureComparePass.halfpixelLocation,
                                                QVector2D(0.5 / blitTexture->width(), 0.5 / blitTexture->height()));

        // pixels at window borders are fairly unreliable so ignore a slim border (1% of the texture size)
        m_textureComparePass.shader->setUniform(m_textureComparePass.borderIgnore, 0.01);

        GLuint query;
        glGenQueries(1, &query);

        // don't acctually draw anything
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        // check for non-discarded pixels
        // GL_ANY_SAMPLES_PASSED_CONSERVATIVE is supposedly faster but
        // implementation dependent, may have false positives (meaning cache invalidation when not needed)
        // and needs new-ish OpenGL 4.3 (https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBeginQuery.xhtml)
        // so let's just use the slightly slower GL_ANY_SAMPLES_PASSED (OpenGL 3.3)
        glBeginQuery(GL_ANY_SAMPLES_PASSED, query);

        // if the query isn't available just invalidate, not much we can do here
        if (glGetError() == GL_INVALID_ENUM) [[unlikely]] {
            qCWarning(BLUR_CACHE) << "OpenGL error: GL_ANY_SAMPLES_PASSED query not available";
            m_glQueryAvailable = false;
            glEndQuery(GL_ANY_SAMPLES_PASSED);
            goto cleanup;
        }

        // perform query
        vbo->draw(GL_TRIANGLES, 0, 6);
        glEndQuery(GL_ANY_SAMPLES_PASSED);

        // await query and check
        GLuint anyPixelsDifferent;
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &anyPixelsDifferent);
        if (anyPixelsDifferent == GL_FALSE) {
            qCDebug(BLUR_CACHE) << "Found cache entry for" << cacheData.w->windowClass();
            cacheData.lru.select();
        }

cleanup:
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDeleteQueries(1, &query);
        glActiveTexture(GL_TEXTURE0);

        KWin::GLFramebuffer::popFramebuffer();
        KWin::ShaderManager::instance()->popShader();
    }
}

void BBDX::BlurCache::setupVBO(const KWin::Rect &scaledBackgroundRect, std::span<KWin::GLVertex2D> &map, size_t &vboIndex) const {
    // The geometry used for the cache, in logical pixels
    // but scaled to what would be drawn on the device.
    {
        const QRectF localRect = QRectF(0, 0, scaledBackgroundRect.width(), scaledBackgroundRect.height());

        const float x0 = localRect.left();
        const float y0 = localRect.top();
        const float x1 = localRect.right();
        const float y1 = localRect.bottom();

        const float u0 = x0 / scaledBackgroundRect.width();
        const float v0 = 1.0f - y0 / scaledBackgroundRect.height();
        const float u1 = x1 / scaledBackgroundRect.width();
        const float v1 = 1.0f - y1 / scaledBackgroundRect.height();

        // first triangle
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x0, y0),
            .texcoord = QVector2D(u0, v0),
        };
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x1, y1),
            .texcoord = QVector2D(u1, v1),
        };
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x0, y1),
            .texcoord = QVector2D(u0, v1),
        };

        // second triangle
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x0, y0),
            .texcoord = QVector2D(u0, v0),
        };
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x1, y0),
            .texcoord = QVector2D(u1, v0),
        };
        map[vboIndex++] = KWin::GLVertex2D{
            .position = QVector2D(x1, y1),
            .texcoord = QVector2D(u1, v1),
        };
    }
};

void BBDX::BlurCache::drawCached(const KWin::Rect &scaledBackgroundRect, const KWin::RenderViewport &viewport, KWin::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const {
    KWin::ShaderManager::instance()->pushShader(m_texturePass.shader.get());
    
    QMatrix4x4 projectionMatrix = viewport.projectionMatrix();
    projectionMatrix.translate(scaledBackgroundRect.x(), scaledBackgroundRect.y());

    KWin::GLTexture* read;
    if (const auto &cacheEntry = renderInfo.cache.lru.valid()) {
        read = cacheEntry->cachedTexture.get();
    } else {
        // bail if we didn't select or add a cache entry
        qCritical(BLUR_CACHE) << "drawCached() called without a valid cache entry";
        KWin::ShaderManager::instance()->popShader();
        return;
    }

    m_texturePass.shader->setUniform(m_texturePass.mvpMatrixLocation, projectionMatrix);
    read->bind();

    if (modulation < 1.0) {
        glEnable(GL_BLEND);
        glBlendColor(0, 0, 0, modulation);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    }

    vbo->draw(GL_TRIANGLES, 12, vertexCount);

    if (modulation < 1.0) {
        glDisable(GL_BLEND);
    }

    KWin::ShaderManager::instance()->popShader();
}

void BBDX::BlurCache::drawToCache(KWin::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo) const {
    auto cachedFramebuffer = renderInfo.cache.lru.valid()->cachedFramebuffer.get();
    KWin::GLFramebuffer::pushFramebuffer(cachedFramebuffer);
    vbo->draw(GL_TRIANGLES, 6, 6);
    KWin::GLFramebuffer::popFramebuffer();
}

std::unique_ptr<KWin::GLTexture> BBDX::BlurCache::cloneBlitTexture(KWin::BlurRenderData &renderInfo) const {
    KWin::GLFramebuffer *sourceBuffer = renderInfo.framebuffers[0].get();
    KWin::GLTexture *sourceTexture = sourceBuffer->colorAttachment();

    auto texture = KWin::GLTexture::allocate(sourceTexture->internalFormat(), sourceTexture->size());
    if (!texture) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to allocate an offscreen texture";
        return nullptr;
    }
    texture->setFilter(GL_LINEAR);
    texture->setWrapMode(GL_CLAMP_TO_EDGE);

    auto framebuffer = std::make_unique<KWin::GLFramebuffer>(texture.get());
    if (!framebuffer->valid()) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
        return nullptr;
    }

    KWin::GLFramebuffer::pushFramebuffer(sourceBuffer);
    framebuffer->blitFromFramebuffer();
    KWin::GLFramebuffer::popFramebuffer();

    return texture;
}
