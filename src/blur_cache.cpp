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

    // just in case KWin does something funky and gives us a
    // (incomplete) glTexImage2D instead of a (complete) glTexStorage2D
    // we need to make it "complete" by marking level 0 as the only available level
    // else glBindImageTexture can't use it in image load/store contexts
    // https://wikis.khronos.org/opengl/Texture#Texture_completeness
    entry->blitTexture->bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    entry->blitTexture->unbind();

    entry->blitFramebuffer = std::make_unique<KWin::GLFramebuffer>(entry->blitTexture.get());
    if (!entry->blitFramebuffer->valid()) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create an offscreen framebuffer";
        return nullptr;
    }
    KWin::GLFramebuffer::pushFramebuffer(entry->blitFramebuffer.get());
    glClear(GL_COLOR_BUFFER_BIT);
    KWin::GLFramebuffer::popFramebuffer();

    // copy data from dirtyRegion
    KWin::GLFramebuffer::pushFramebuffer(dirtyBlitFramebuffer);
    for (const auto &rect : entry->localDirtyRegion(dirtyRegion).rects()) {
        entry->blitFramebuffer->blitFromFramebuffer(rect, rect);
    }
    KWin::GLFramebuffer::popFramebuffer();

    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX << "New BlurCacheEntry:\n"
                                            << "dirtyRegion:" << dirtyRegion;

    return entry;
}

void BBDX::BlurCacheEntry::accumulateDirtyRegion(const KWin::Region &dirtyRegion) {
    for (const auto &rect : dirtyRegion.rects()) {
        accumulatedDirtyRegion |= rect;
    }

    // we only care about dirtyRegion that has blur
    accumulatedDirtyRegion &= backgroundRect;
}

KWin::Region BBDX::BlurCacheEntry::localDirtyRegion(const KWin::Region &dirtyRegion) const {
    return dirtyRegion.translated(-backgroundRect.topLeft());
}

void BBDX::BlurCacheEntry::flush() {
    isFlushing = true;
}


void BBDX::BlurCacheEntry::abortFlush(const char* msg) {
    if (isFlushing) {
        isFlushing = false;
        if (msg) {
            qCDebug(BLUR_CACHE) << "Aborted flush:" << msg;
        }
    }
}

void BBDX::BlurCacheEntry::flushed() {
    if (isFlushing) {
        accumulatedDirtyRegion = KWin::Region{};
        lastFlush = std::chrono::steady_clock::now();
        isFlushing = false;
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

BBDX::BlurCache::BlurCache(BBDX::BlurEffect *effect) {
    m_effect = effect;

    m_texturePass.shader = KWin::ShaderManager::instance()->generateShaderFromFile(KWin::ShaderTrait::MapTexture,
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/vertex.vert"),
                                                                           QStringLiteral(":/effects/better_blur_dx/shaders/texture.frag"));
    if (!m_texturePass.shader) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to load texture pass shader";
        return;
    } else {
        m_texturePass.mvpMatrixLocation = m_texturePass.shader->uniformLocation("modelViewProjectionMatrix");
    }

    m_textureComparer = TextureComparer::create();
    if (!m_textureComparer) {
        qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Failed to create TextureComparer";
        return;
    }
}

void BBDX::BlurCache::preparePaintData(const KWin::RenderView *view,
                                       const KWin::EffectWindow *window,
                                       const KWin::Region *dirtyRegion,
                                       KWin::GLFramebuffer *blitFramebuffer,
                                       const KWin::Rect *backgroundRect,
                                       const KWin::Rect *scaledBackgroundRect,
                                       BlurCacheLRU &cache) {
    m_paintData.view = view;
    m_paintData.window = window;
    m_paintData.dirtyRegion = dirtyRegion;
    m_paintData.blitFramebuffer = blitFramebuffer;
    m_paintData.backgroundRect = backgroundRect;
    m_paintData.scaledBackgroundRect = scaledBackgroundRect;
    m_paintData.glBeginConditionalRenderCalled = false;

    // the cache entry needs to stay in sync
    // so BlurCacheEntry::localDirtyRegion() returns
    // correct info
    if (auto cacheEntry = cache.get()) {
        cacheEntry->backgroundRect = *backgroundRect;
        cacheEntry->accumulateDirtyRegion(*dirtyRegion);

        // still not sure if dirtyRegion can even end up empty
        // but if it is a flush would always end up taking the cache anyway
        // (no changes to compare). this at least skips some compute
        if (dirtyRegion->isEmpty() && cacheEntry->isFlushing) {
            cacheEntry->abortFlush("Empty dirtyRegion");
        }
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

void BBDX::BlurCache::prepareCache(BBDX::BlurCacheLRU &cache) {
    // if we don't have an entry create one and bail to fill it
    auto cacheEntry = cache.get();
    if (!cacheEntry) {
        auto newCacheEntry = BBDX::BlurCacheEntry::create(*m_paintData.scaledBackgroundRect,
                                                          m_paintData.blitFramebuffer,
                                                          *m_paintData.dirtyRegion,
                                                          *m_paintData.backgroundRect);
        // XXX: ensure this is safe
        // and BlurEffect::blur() bails
        // if this fails or we get nullptr derefs when trying to
        // access blit/target framebuffers
        if (!newCacheEntry) {
            qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Creating BlurCacheEntry failed";
            return;
        }

        // flush the new entry immediately
        // bypassing texture compare
        newCacheEntry->flush();

        cache.add(std::move(newCacheEntry));

        return;
    }

    // only proceed if marked in flushAccumulatedDirtyRegions()
    if (!cacheEntry->isFlushing) {
        return;
    }

    
    /*
     * TODO: texture compare seems to be broken on some GPUs
     * (AMD RDNA2 was reported the most) so skip it for now
     * and just force a blur refresh
     */
    KWin::GLFramebuffer::pushFramebuffer(m_paintData.blitFramebuffer);
    for (const auto &rect : cacheEntry->localDirtyRegion(*m_paintData.dirtyRegion).rects()) {
        cacheEntry->blitFramebuffer->blitFromFramebuffer(rect, rect, GL_NEAREST);
    }
    KWin::GLFramebuffer::popFramebuffer();

    /*
    auto textureCompareWindowData = cache.textureCompareWindowData();
    if (!textureCompareWindowData) [[unlikely]] {
        // GL resource alloc failed
        return;
    }

    const auto textureCompareWindowDataSlot = textureCompareWindowData->getSlot();
    if (!textureCompareWindowDataSlot) {
        // we didn't get a slot (meaning all queries are busy)
        // abort this flush
        cacheEntry->abortFlush("All queries busy");
        return;
    }

    const auto newTexture = m_paintData.blitFramebuffer->colorAttachment();
    const auto cachedTexture = cacheEntry->blitTexture.get();

    const auto ret = m_textureComparer->compareAndUpdate(*textureCompareWindowDataSlot,
                                                         newTexture,
                                                         cachedTexture,
                                                         m_paintData);

    // something went wrong
    if (!ret) {
        cacheEntry->abortFlush("TextureComparer::compareAndUpdate() failed");
        return;
    }

    // await the query from TextureComparer::compareAndUpdate()
    glBeginConditionalRender(textureCompareWindowDataSlot->second, GL_QUERY_BY_REGION_WAIT);
    m_paintData.glBeginConditionalRenderCalled = true;
    */
}

void BBDX::BlurCache::drawCached(const KWin::RenderViewport &viewport, BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const {
    // end glBeginConditionalRender from prepareCache()
    if (m_paintData.glBeginConditionalRenderCalled) {
        glEndConditionalRender();
    }

    const auto &scaledBackgroundRect = *m_paintData.scaledBackgroundRect;

    KWin::ShaderManager::instance()->pushShader(m_texturePass.shader.get());
    
    QMatrix4x4 projectionMatrix = viewport.projectionMatrix();
    projectionMatrix.translate(scaledBackgroundRect.x(), scaledBackgroundRect.y());

    KWin::GLTexture* read;
    if (const auto &cacheEntry = renderInfo.cache.get()) {
        read = cacheEntry->cachedTexture.get();
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
    auto cachedFramebuffer = cache.get()->cachedFramebuffer.get();
    KWin::GLFramebuffer::pushFramebuffer(cachedFramebuffer);
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
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cacheEntry->lastFlush);
            if (elapsed.count() < 33) {
                continue;
            }

            for (const auto &rect : cacheEntry->accumulatedDirtyRegion.rects()) {
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
