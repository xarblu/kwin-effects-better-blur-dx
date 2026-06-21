#include "blur_cache.hpp"

#include "kwin_compat.hpp"

#include "blur.h"
#include "utils.h"
#include "texture_comparer.hpp"

#include <epoxy/gl.h>
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

#include <chrono>
#include <memory>

Q_LOGGING_CATEGORY(BLUR_CACHE, "kwin_effect_better_blur_dx.blur_cache", QtInfoMsg)


/**
 * Update the cached blit texture in blitFramebuffer
 * with contents of the given dirtyRegion
 */
static inline void updateBlitFramebuffer(const KWin::RenderTarget &renderTarget,
                                         const KWin::RenderViewport &viewport,
                                         KWin::GLFramebuffer *blitFramebuffer,
                                         const KWin::Region &dirtyRegion,
                                         const KWin::Rect &backgroundRect) {
    for (const auto &rect : dirtyRegion.rects()) {
        blitFramebuffer->blitFromRenderTarget(renderTarget,
                                              viewport,
                                              rect,
                                              rect.translated(-backgroundRect.topLeft()));
    }
}

std::unique_ptr<BBDX::BlurCacheEntry> BBDX::BlurCacheEntry::create(const KWin::Rect &scaledBackgroundRect,
                                                                   const KWin::GLFramebuffer *dirtyBlitFramebuffer) {
    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX << "New BlurCacheEntry with size:" << scaledBackgroundRect;

    std::unique_ptr<BlurCacheEntry> entry{new BlurCacheEntry()};

    // allocate new cached texture + framebuffer for the blurred texture
    glClearColor(0, 0, 0, 0);
    entry->m_cachedTexture = KWin::GLTexture::allocate(dirtyBlitFramebuffer->colorAttachment()->internalFormat(), scaledBackgroundRect.size());
    if (!entry->m_cachedTexture) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to allocate an offscreen texture";
        return nullptr;
    }
    entry->m_cachedTexture->setFilter(GL_LINEAR);
    entry->m_cachedTexture->setWrapMode(GL_CLAMP_TO_EDGE);

    entry->m_cachedFramebuffer = std::make_unique<KWin::GLFramebuffer>(entry->m_cachedTexture.get());
    if (!entry->m_cachedFramebuffer->valid()) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
        return nullptr;
    }
    KWin::GLFramebuffer::pushFramebuffer(entry->m_cachedFramebuffer.get());
    glClear(GL_COLOR_BUFFER_BIT);
    KWin::GLFramebuffer::popFramebuffer();

    return entry;
}

void BBDX::BlurCacheEntry::accumulateDirtyRegion(const KWin::Region &dirtyRegion) {
    for (const auto &rect : dirtyRegion.rects()) {
        m_accumulatedDirtyRegion |= rect;
    }

    // we only care about dirtyRegion that has blur
    m_accumulatedDirtyRegion &= m_backgroundRect;
}

KWin::Region BBDX::BlurCacheEntry::localDirtyRegion(const KWin::Region &dirtyRegion) const {
    return dirtyRegion.translated(-m_backgroundRect.topLeft());
}

void BBDX::BlurCacheEntry::flush() {
    m_isFlushing = true;
}


void BBDX::BlurCacheEntry::abortFlush(const char* msg) {
    if (m_isFlushing) {
        m_isFlushing = false;
        if (msg) {
            qCDebug(BLUR_CACHE) << "Aborted flush:" << msg;
        }
    }
}

void BBDX::BlurCacheEntry::flushed() {
    if (m_isFlushing) {
        m_accumulatedDirtyRegion = KWin::Region{};
        m_lastFlush = std::chrono::steady_clock::now();
        m_isFlushing = false;
    }
}

BBDX::BlurCacheEntry* BBDX::BlurCacheLRU::get() {
    return m_entry.get();
}

BBDX::TextureComparer::WindowData* BBDX::BlurCacheLRU::textureCompareWindowData() {
    // alloc only happens once per Window+RenderView combination
    if (!m_textureCompareWindowData) [[unlikely]] {
        m_textureCompareWindowData = TextureComparer::WindowData::create();

        if (!m_textureCompareWindowData) {
            qCCritical(BLUR_CACHE) << "Failed to create TextureComparer::WindowData";
            return nullptr;
        }
    }

    return m_textureCompareWindowData.get();
}

void BBDX::BlurCacheLRU::add(std::unique_ptr<BlurCacheEntry> entry) {
    if (m_entry) {
        qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX
                            << "Replacing BlurCacheEntry:" << m_windowClass << "\n"
                            << "PID:" << m_windowPID << "\n";
    } else {
        qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX
                            << "Adding BlurCacheEntry:" << m_windowClass << "\n"
                            << "PID:" << m_windowPID << "\n";
    }

    m_entry = std::move(entry);
}

void BBDX::BlurCacheLRU::invalidate(QStringView reason, bool skipGlContext) {
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

    m_entry.reset();
}

void BBDX::BlurCacheLRU::setWindow(KWin::EffectWindow* w) {
    if (m_window) {
        return;
    }

    m_window = w;
    m_windowClass = m_window->windowClass();
    m_windowPID = m_window->pid();
}

std::unique_ptr<BBDX::BlurCache> BBDX::BlurCache::create(BBDX::BlurEffect *effect) {
    std::unique_ptr<BlurCache> blurCache{new BlurCache};

    blurCache->m_effect = effect;

    blurCache->m_texturePass.shader = KWin::ShaderManager::instance()->generateShaderFromFile(KWin::ShaderTrait::MapTexture,
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/vertex.vert"),
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/texture.frag"));
    if (!blurCache->m_texturePass.shader) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to load texture pass shader";
        return nullptr;
    } else {
        blurCache->m_texturePass.mvpMatrixLocation = blurCache->m_texturePass.shader->uniformLocation("modelViewProjectionMatrix");
    }

    return blurCache;
}

void BBDX::BlurCache::preparePaintData(const KWin::RenderTarget *renderTarget,
                                       const KWin::RenderViewport *viewport,
                                       const KWin::RenderView *view,
                                       const KWin::EffectWindow *window,
                                       const KWin::Region *dirtyRegion,
                                       KWin::GLFramebuffer *blitFramebuffer,
                                       const KWin::Rect *backgroundRect,
                                       const KWin::Rect *scaledBackgroundRect,
                                       BlurCacheLRU &cache) {
    m_paintData = {
        .renderTarget = renderTarget,
        .viewport = viewport,
        .view = view,
        .window = window,
        .dirtyRegion = dirtyRegion,
        .backgroundRect = backgroundRect,
        .scaledBackgroundRect = scaledBackgroundRect,
        .blitFramebuffer = blitFramebuffer,
    };

    // create new cache entry if needed
    if (!cache.get()) {
        auto newCacheEntry = BBDX::BlurCacheEntry::create(*m_paintData.scaledBackgroundRect,
                                                          m_paintData.blitFramebuffer);
        // XXX: ensure this is safe
        // and BlurEffect::blur() bails
        // if this fails or we get nullptr derefs when trying to
        // access blit/target framebuffers
        if (!newCacheEntry) {
            qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Creating BlurCacheEntry failed";
            return;
        }

        // flush the new entry immediately
        newCacheEntry->flush();

        cache.add(std::move(newCacheEntry));
    }

    // by now we are guaranteed to have an entry
    auto cacheEntry = cache.get();

    // the cache entry needs to stay in sync
    // so BlurCacheEntry::localDirtyRegion() returns
    // correct info
    cacheEntry->setBackgroundRect(*backgroundRect);
    cacheEntry->accumulateDirtyRegion(*dirtyRegion);

    // still not sure if dirtyRegion can even end up empty
    // but if it is a flush would always end up taking the cache anyway
    // (no changes to compare). this at least skips some compute
    if (dirtyRegion->isEmpty() && cacheEntry->isFlushing()) {
        cacheEntry->abortFlush("Empty dirtyRegion");
    }

    // when flushing we need the updated blit
    if (cacheEntry->isFlushing()) {
        updateBlitFramebuffer(*m_paintData.renderTarget,
                              *m_paintData.viewport,
                              m_paintData.blitFramebuffer,
                              *m_paintData.dirtyRegion,
                              *m_paintData.backgroundRect);
    }
}

void BBDX::BlurCache::setupVBO(std::span<KWin::GLVertex2D> &map, size_t &vboIndex) const {
    const auto scaledBackgroundRect = m_paintData.scaledBackgroundRect;

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

void BBDX::BlurCache::drawCached(const KWin::RenderViewport &viewport, BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const {
    const auto &scaledBackgroundRect = *m_paintData.scaledBackgroundRect;

    KWin::ShaderManager::instance()->pushShader(m_texturePass.shader.get());
    
    QMatrix4x4 projectionMatrix = viewport.projectionMatrix();
    projectionMatrix.translate(scaledBackgroundRect.x(), scaledBackgroundRect.y());

    KWin::GLTexture* read;
    if (const auto &cacheEntry = renderInfo.cache.get()) {
        read = cacheEntry->cachedTexture();
        cacheEntry->flushed();
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
    auto cachedFramebuffer = cache.get()->cachedFramebuffer();
    KWin::GLFramebuffer::pushFramebuffer(cachedFramebuffer);
    BBDX::setGLScissor(*m_paintData.dirtyRegion, *m_paintData.backgroundRect);
    vbo->draw(GL_TRIANGLES, vboStartCache(), vboCountCache());
    KWin::GLFramebuffer::popFramebuffer();
}


void BBDX::BlurCache::flushAccumulatedDirtyRegions(KWin::ScreenPrePaintData &data) const {
    for (auto &[window, effectData] : m_effect->m_windows) {
        for (auto &[view, renderData] : effectData.render) {
#if defined(BETTERBLUR_X11)
            if (view != data.screen) {
                continue;
            }
#else
            if (view != data.view) {
                continue;
            }
#endif

            auto cacheEntry = renderData.cache.get();
            if (!cacheEntry) {
                continue;
            }

            // flush at ~30fps
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cacheEntry->lastFlush());
            if (elapsed.count() < 33) {
                continue;
            }

            for (const auto &rect : cacheEntry->accumulatedDirtyRegion().rects()) {
                data.paint |= rect;
            }

            // we'll always flush here
            // it should essentially be a no-op if there was no
            // accumulatedDirtyRegion but ensures prepareCache()
            // still checks new dirtyRegion ASAP when the timer elapsed
            cacheEntry->flush();
        }
    }
}
