#include "blur_cache.hpp"

#include "kwin_compat.hpp"

#include "blur.h"
#include "utils.h"

#include <epoxy/gl.h>
#include <epoxy/gl_generated.h>
#include <qloggingcategory.h>
#include <scene/scene.h>
#include <sys/types.h>

#include <core/pixelgrid.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/eglcontext.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#if KWIN_VERSION >= KWIN_VERSION_CODE(6, 5, 80)
#  include <core/rect.h>
#endif

#include <QLoggingCategory>
#include <QVector2D>
#include <QtNumeric>

#include <memory>
#include <array>

Q_LOGGING_CATEGORY(BLUR_CACHE, "kwin_effect_better_blur_dx.blur_cache", QtInfoMsg)


std::unique_ptr<BBDX::BlurCacheEntry> BBDX::BlurCacheEntry::create(const KWin::Rect &scaledBackgroundRect,
                                                                   KWin::GLFramebuffer *dirtyBlitFramebuffer,
                                                                   KWin::Region dirtyRegion,
                                                                   KWin::Rect backgroundRect) {
    auto entry = std::make_unique<BBDX::BlurCacheEntry>();
    entry->backgroundRect = backgroundRect;

    // allocate new cached texture + framebuffer for the blurred texture
    glClearColor(0, 0, 0, 0);
    entry->cachedTexture = KWin::GLTexture::allocate(dirtyBlitFramebuffer->colorAttachment()->internalFormat(), scaledBackgroundRect.size());
    if (!entry->cachedTexture) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to allocate an offscreen texture";
        return nullptr;
    }
    entry->cachedTexture->setFilter(GL_LINEAR);
    entry->cachedTexture->setWrapMode(GL_CLAMP_TO_EDGE);

    entry->cachedFramebuffer = std::make_unique<KWin::GLFramebuffer>(entry->cachedTexture.get());
    if (!entry->cachedFramebuffer->valid()) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
        return nullptr;
    }
    KWin::GLFramebuffer::pushFramebuffer(entry->cachedFramebuffer.get());
    glClear(GL_COLOR_BUFFER_BIT);
    KWin::GLFramebuffer::popFramebuffer();

    // allocate new cached texture + framebuffer for the raw blit texture
    KWin::GLTexture *dirtyTexture = dirtyBlitFramebuffer->colorAttachment();

    entry->blitTexture = KWin::GLTexture::allocate(dirtyTexture->internalFormat(), dirtyTexture->size());
    if (!entry->blitTexture) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to allocate an offscreen texture";
        return nullptr;
    }
    entry->blitTexture->setFilter(GL_LINEAR);
    entry->blitTexture->setWrapMode(GL_CLAMP_TO_EDGE);

    entry->blitFramebuffer = std::make_unique<KWin::GLFramebuffer>(entry->blitTexture.get());
    if (!entry->blitFramebuffer->valid()) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
        return nullptr;
    }
    KWin::GLFramebuffer::pushFramebuffer(entry->blitFramebuffer.get());
    glClear(GL_COLOR_BUFFER_BIT);
    KWin::GLFramebuffer::popFramebuffer();

    // copy data from dirtyRegion
    entry->updateBlitTexture(dirtyBlitFramebuffer, dirtyRegion);

    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX << "New BlurCacheEntry:\n"
                                            << "dirtyRegion:" << dirtyRegion;

    return entry;
}

void BBDX::BlurCacheEntry::updateBlitTexture(KWin::GLFramebuffer *dirtyBlitFramebuffer, const KWin::Region &dirtyRegion) {
    KWin::GLFramebuffer::pushFramebuffer(dirtyBlitFramebuffer);
    for (const auto &rect : localDirtyRegion(dirtyRegion).rects()) {
        blitFramebuffer->blitFromFramebuffer(rect, rect);
    }
    KWin::GLFramebuffer::popFramebuffer();
}

KWin::Region BBDX::BlurCacheEntry::localDirtyRegion(const KWin::Region &dirtyRegion) const {
    return dirtyRegion.translated(-backgroundRect.topLeft());
}

BBDX::BlurCacheEntry* BBDX::BlurCacheLRU::get() {
    return m_entry.get();
}

void BBDX::BlurCacheLRU::add(std::unique_ptr<BlurCacheEntry> entry) {
    if (m_entry) {
        qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX
                            << "Replacing BlurCacheEntry:" << m_windowClass << "\n"
                            << "PID:" << m_windowPID << "\n"
                            << "Hits:" << m_entry->hits;
    } else {
        qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX
                            << "Adding BlurCacheEntry:" << m_windowClass << "\n"
                            << "PID:" << m_windowPID << "\n";
    }

    m_entry = std::move(entry);
    m_entry->priority = 0;
}

void BBDX::BlurCacheLRU::invalidate(BlurCacheInvalidation type, QStringView reason, bool skipGlContext) {
    if (!m_entry) {
        return;
    }

    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX
                        << "Invalidating cache:" << m_windowClass << "\n"
                        << "PID:" << m_windowPID << "\n"
                        << "Reason:" << reason;

    // invalidate can be called from various events outside
    // the window paint pipeline so we need to explicitly
    // make the context current to correctly drop framebuffers/textures
    if (!skipGlContext) {
        KWin::effects->makeOpenGLContextCurrent();
    }
}

void BBDX::BlurCacheLRU::setWindow(KWin::EffectWindow* w) {
    if (m_window) {
        return;
    }

    m_window = w;
    m_windowClass = m_window->windowClass();
    m_windowPID = m_window->pid();
}

BBDX::BlurCache::BlurCache(BBDX::BlurEffect *effect) {
    m_effect = effect;

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

void BBDX::BlurCache::preparePaintData(const KWin::RenderView *view,
                                       const KWin::EffectWindow *window,
                                       const KWin::Region *dirtyRegion,
                                       const KWin::GLFramebuffer *blitFramebuffer,
                                       const KWin::Rect *backgroundRect,
                                       const KWin::Rect *scaledBackgroundRect,
                                       BlurCacheLRU &cache) {
    m_paintData.view = view;
    m_paintData.window = window;
    m_paintData.dirtyRegion = dirtyRegion;
    m_paintData.blitFramebuffer = blitFramebuffer;
    m_paintData.backgroundRect = backgroundRect;
    m_paintData.scaledBackgroundRect = scaledBackgroundRect;
    m_paintData.textureCompareRegion = KWin::Region{};
    m_paintData.glBeginConditionalRenderCalled = false;

    for (const auto &rect : dirtyRegion->rects()) {
        m_paintData.textureCompareRegion |= rect.translated(-backgroundRect->topLeft());
    }
    m_paintData.textureCompareVertexCount = m_paintData.textureCompareRegion.rects().size() * 6;

    // the cache entry needs to stay in sync
    // so BlurCacheEntry::localDirtyRegion() returns
    // correct info
    if (auto cacheEntry = cache.get()) {
        cacheEntry->backgroundRect = *backgroundRect;
    }
}

void BBDX::BlurCache::setupVBO(std::span<KWin::GLVertex2D> &map, size_t &vboIndex) const {
    auto backgroundRect = m_paintData.backgroundRect;
    auto scaledBackgroundRect = m_paintData.scaledBackgroundRect;
    auto &textureCompareRegion = m_paintData.textureCompareRegion;

    // The geometry used for texture comparison, in logical pixels
    // relative to backgroundRect
    for (const auto &rect : textureCompareRegion.rects()) {
        const float textureWidth = backgroundRect->width();
        const float textureHeight = backgroundRect->height();

        const float x0 = rect.left();
        const float y0 = rect.top();
        const float x1 = rect.right();
        const float y1 = rect.bottom();

        const float u0 = x0 / textureWidth;
        const float v0 = 1.0f - y0 / textureHeight;
        const float u1 = x1 / textureWidth;
        const float v1 = 1.0f - y1 / textureHeight;

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

    // The geometry used for the cache, in logical pixels
    // but scaled to what would be drawn on the device.
    {
        const QRectF localRect = QRectF(0, 0, scaledBackgroundRect->width(), scaledBackgroundRect->height());

        const float x0 = localRect.left();
        const float y0 = localRect.top();
        const float x1 = localRect.right();
        const float y1 = localRect.bottom();

        const float u0 = x0 / scaledBackgroundRect->width();
        const float v0 = 1.0f - y0 / scaledBackgroundRect->height();
        const float u1 = x1 / scaledBackgroundRect->width();
        const float v1 = 1.0f - y1 / scaledBackgroundRect->height();

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
}

void BBDX::BlurCache::prepareCache(BBDX::BlurCacheLRU &cache,
                                   KWin::GLVertexBuffer *vbo) {
    if (!m_glQueryObject) {
        m_glQueryObjects = std::make_unique<std::array<GLuint, QUERY_OBJECT_COUNT>, GLQueryObjectDeleter>();
        glGenQueries(m_glQueryObjects->size(), m_glQueryObjects->data());
    }

    // if we don't have an entry create one and bail to fill it
    auto cacheEntry = cache.get();
    if (!cacheEntry) {
        auto newCacheEntry = BBDX::BlurCacheEntry::create(m_paintData.scaledBackgroundRect,
                                                          m_paintData.blitFramebuffer,
                                                          m_paintData.dirtyRegion,
                                                          m_paintData.backgroundRect);
        // XXX: ensure this is safe
        // and BlurEffect::blur() bails
        // if this fails or we get nullptr derefs when trying to
        // access blit/target framebuffers
        if (!newCacheEntry) {
            qCWarning(KWIN_BLUR) << BBDX::LOG_PREFIX << "Creating BlurCacheEntry failed";
            return;
        }

        cache.add(newCacheEntry);

        return;
    }

    // fast path in case we already determined we
    // can't perform texture comparison
    if (m_glQueryAvailable == GLQueryAvailable::NONE) [[unlikely]] {
        return;
    }

    // Somehow we can end up here with an empty textureCompareRegion
    // which would mean there was no dirtyRegion and thus no blitted data.
    //
    // TODO: currently this just causes re-blur 
    if (m_paintData.textureCompareRegion.isEmpty()) {
        return;
    }

    // QUERY START

    // textures + FBOs used in query
    auto cachedTextureFBO = std::pair{cacheEntry->blitTexture, cacheEntry->blitFramebuffer};
    auto newTextureFBO = std::pair{m_paintData.blitFramebuffer->colorAttachment(), m_paintData.blitFramebuffer};
    auto &[cachedTexture, cachedFramebuffer] = cachedTextureFBO;
    auto &[newTexture, newFramebuffer] = newTextureFBO;

    // check if textures differ on the pixel level
    KWin::ShaderManager::instance()->pushShader(m_textureComparePass.shader.get());

    // Use FBO of the cached blit; the query's draw will also
    // update this with pixels from the new blit (if it differs)
    KWin::GLFramebuffer::pushFramebuffer(cachedFramebuffer.get());

    QMatrix4x4 projectionMatrix;
    projectionMatrix.ortho(QRectF(0.0, 0.0, newTexture->width(), newTexture->height()));

    m_textureComparePass.shader->setUniform(m_textureComparePass.mvpMatrixLocation, projectionMatrix);

    m_textureComparePass.shader->setUniform(m_textureComparePass.texUnitOldLocation, 0);
    glActiveTexture(GL_TEXTURE0);
    cachedTexture->bind();

    m_textureComparePass.shader->setUniform(m_textureComparePass.texUnitNewLocation, 1);
    glActiveTexture(GL_TEXTURE1);
    newTexture->bind();

    // grab query object from available query objects
    GLuint *queryObject = &m_glQueryObjects[m_nextGlQueryObject++];
    if (m_nextGlQueryObject >= m_glQueryObjects.size()) {
        m_nextGlQueryObject = 0;
    }

    // pick the first available query in preferred order (based on supposed speed)
    // https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBeginQuery.xhtml
    GLenum queryUsed{};
    switch (m_glQueryAvailable) {
        case GLQueryAvailable::ANY_SAMPLES_PASSED_CONSERVATIVE:
            glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE, *m_glQueryObject);
            if (glGetError() == GL_NO_ERROR) [[likely]] {
                queryUsed = GL_ANY_SAMPLES_PASSED_CONSERVATIVE;
                break;
            }

            qCWarning(BLUR_CACHE) << "OpenGL error: GL_ANY_SAMPLES_PASSED_CONSERVATIVE query not available."
                                  << "Falling back to ANY_SAMPLES_PASSED.";
            m_glQueryAvailable = GLQueryAvailable::ANY_SAMPLES_PASSED;
            [[fallthrough]];

        case GLQueryAvailable::ANY_SAMPLES_PASSED:
            glBeginQuery(GL_ANY_SAMPLES_PASSED, *m_glQueryObject);
            if (glGetError() == GL_NO_ERROR) [[likely]] {
                queryUsed = GL_ANY_SAMPLES_PASSED;
                break;
            }

            qCWarning(BLUR_CACHE) << "OpenGL error: GL_ANY_SAMPLES_PASSED query not available."
                                  << "Falling back to SAMPLES_PASSED.";
            m_glQueryAvailable = GLQueryAvailable::SAMPLES_PASSED;
            [[fallthrough]];

        case GLQueryAvailable::SAMPLES_PASSED:
            glBeginQuery(GL_SAMPLES_PASSED, *m_glQueryObject);
            if (glGetError() == GL_NO_ERROR) [[likely]] {
                queryUsed = GL_SAMPLES_PASSED;
                break;
            }

            qCWarning(BLUR_CACHE) << "OpenGL error: GL_SAMPLES_PASSED query not available."
                                  << "No more fallbacks.";
            m_glQueryAvailable = GLQueryAvailable::NONE;
            [[fallthrough]];

        [[unlikely]] default:
            goto cleanup;
    }

    vbo->draw(GL_TRIANGLES, vboStartTextureCompare(), vboCountTextureCompare());

    glEndQuery(queryUsed);

    glBeginConditionalRender(*m_glQueryObject, GL_QUERY_BY_REGION_WAIT);
    m_paintData.glBeginConditionalRenderCalled = true;

    // our query implicitly updates the blur source, this
    // makes sure subsequent blur passes get the correct one
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);

cleanup:
    glActiveTexture(GL_TEXTURE0);

    KWin::GLFramebuffer::popFramebuffer();
    KWin::ShaderManager::instance()->popShader();

    // QUERY END
}

void BBDX::BlurCache::drawCached(const KWin::Rect &scaledBackgroundRect, const KWin::RenderViewport &viewport, BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const {
    // end glBeginConditionalRender from prepareCache()
    if (m_paintData.glBeginConditionalRenderCalled) {
        glEndConditionalRender();
    }

    KWin::ShaderManager::instance()->pushShader(m_texturePass.shader.get());
    
    QMatrix4x4 projectionMatrix = viewport.projectionMatrix();
    projectionMatrix.translate(scaledBackgroundRect.x(), scaledBackgroundRect.y());

    KWin::GLTexture* read;
    if (auto &cacheEntry = renderInfo.cache.get()) {
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

    vbo->draw(GL_TRIANGLES, vboStartScreen(), vertexCount);

    if (modulation < 1.0) {
        glDisable(GL_BLEND);
    }

    KWin::ShaderManager::instance()->popShader();
}

void BBDX::BlurCache::drawToCache(BBDX::BlurCacheLRU &cache, KWin::GLVertexBuffer *vbo) const {
    auto cachedFramebuffer = cache.get()->cachedFramebuffer.get();
    KWin::GLFramebuffer::pushFramebuffer(cachedFramebuffer);
    vbo->draw(GL_TRIANGLES, vboStartCache(), vboCountCache());
    KWin::GLFramebuffer::popFramebuffer();
}
