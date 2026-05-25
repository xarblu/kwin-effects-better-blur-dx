#include "blur_cache.hpp"

#include "blur.h"
#include "kwin_version.hpp"
#include "utils.h"

#include <epoxy/gl.h>
#include <sys/types.h>

#include <core/pixelgrid.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
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

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

Q_LOGGING_CATEGORY(BLUR_CACHE, "kwin_effect_better_blur_dx.blur_cache", QtInfoMsg)


std::unique_ptr<BBDX::BlurCacheEntry> BBDX::BlurCacheEntry::create(const KWin::Rect &scaledBackgroundRect,
                                                                   BBDX::BlurCacheEntry *oldCacheEntry,
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

    // clone the blitTexture from the given existing cache framebuffer, then update it with
    // the dirty region
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

    // check if we're only partially painting
    // if that's the case oldCacheEntry is required
    // to not get a partial texture
    KWin::Region missingPaint{backgroundRect.translated(-backgroundRect.topLeft())};
    for (const auto &rect : entry->localDirtyRegion(dirtyRegion).rects()) {
        missingPaint -= rect;
    }
    bool partialPaint{!missingPaint.isEmpty()};
    
    if (partialPaint && !oldCacheEntry) [[unlikely]] {
        entry->partial = true;
    } else if (oldCacheEntry) [[likely]] {
        KWin::GLFramebuffer::pushFramebuffer(oldCacheEntry->blitFramebuffer.get());
        entry->blitFramebuffer->blitFromFramebuffer();
        KWin::GLFramebuffer::popFramebuffer();
    }

    entry->updateBlitTexture(dirtyBlitFramebuffer, dirtyRegion);

    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX << "New BlurCacheEntry:\n"
                                            << "Partial:" << entry->partial << "\n"
                                            << "dirtyRegion:" << dirtyRegion;

    entry->verifiedAt = std::chrono::steady_clock::now();

    return entry;
}

void BBDX::BlurCacheEntry::updateBlitTexture(KWin::GLFramebuffer *dirtyBlitFramebuffer, KWin::Region dirtyRegion) {
    KWin::GLFramebuffer::pushFramebuffer(dirtyBlitFramebuffer);
    for (const auto &rect : localDirtyRegion(dirtyRegion).rects()) {
        blitFramebuffer->blitFromFramebuffer(rect, rect);
    }
    KWin::GLFramebuffer::popFramebuffer();
}

KWin::Region BBDX::BlurCacheEntry::localDirtyRegion(const KWin::Region &dirtyRegion) const {
    return dirtyRegion.translated(-backgroundRect.topLeft());
}

void BBDX::BlurCacheLRU::reset() {
    m_next = 0;
    m_valid = nullptr;
}

const BBDX::BlurCacheEntry* BBDX::BlurCacheLRU::next() {
    if (m_valid) {
        return nullptr;
    }

    // implicitly handles empty m_entries
    // and reaching the end
    BBDX::BlurCacheEntry* ret{nullptr};
    for (const auto &entry : m_entries) {
        if (entry->priority == m_next) {
            ret = entry.get();
            m_next += 1;
            break;
        }
    }

    return ret;
}

void BBDX::BlurCacheLRU::select(bool verified) {
    if (m_entries.empty()) {
        qCCritical(BLUR_CACHE) << BBDX::LOG_PREFIX
                               << "BlurCacheLRU::select(): Called with no entries";
        return;
    }

    // The only time m_next is 0 is during
    // a call to add() (and technically after reset()).
    // Through usual iteration with next() it will always be >=1
    size_t idx{0};
    if (m_next > 0) {
        idx = m_next - 1;
    }

    BBDX::BlurCacheEntry* selected{nullptr};
    for (const auto &entry : m_entries) {
        if (entry->priority == idx) {
            selected = entry.get();
            break;
        }
    }

    if (!selected) {
        qCCritical(BLUR_CACHE) << BBDX::LOG_PREFIX
                               << "BlurCacheLRU::select(): Could not find entry:" << idx;
        return;
    }

    for (auto &entry : m_entries) {
        if (entry->priority < selected->priority) {
            entry->priority += 1;
        }
    }

    m_next = 0;
    m_valid = selected;
    selected->priority = 0;
    selected->hits += 1;

    if (verified) {
        selected->verifiedAt = std::chrono::steady_clock::now();
    }
}

void BBDX::BlurCacheLRU::add(std::unique_ptr<BlurCacheEntry> entry) {
    m_entries.insert(m_entries.begin(), std::move(entry));

    m_entries[0]->priority = 0;
    m_next = 0;
    select();

    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX
                        << "Adding BlurCacheEntry:" << m_windowClass << "\n"
                        << "PID:" << m_windowPID << "\n"
                        << "Entries:" << m_entries.size() << "of" << m_max;

    for (size_t i = 1; i < m_entries.size(); i++) {
        m_entries[i]->priority += 1;
    }

    // Cleanup excessive cache entries
    //
    // We should be able to assume the limit is exceeded by 1 at most
    // because cleanup happens for each add()
    if (m_entries.size() > m_max) {
        for (auto it = m_entries.begin(); it != m_entries.end(); it++) {
            if ((*it)->priority >= m_max) {
                qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX
                                    << "Dropping BlurCacheEntry:" << m_windowClass << "\n"
                                    << "PID:" << m_windowPID << "\n"
                                    << "Reason: Exceeded limit (" << m_max << ")\n"
                                    << "Hits:" << (*it)->hits;

                m_entries.erase(it);
                break;
            }
        }
    }
}

BBDX::BlurCacheEntry* BBDX::BlurCacheLRU::any() const {
    for (const auto &entry : m_entries) {
        if (!entry->partial) {
            return entry.get();
        }
    }

    return nullptr;
}

void BBDX::BlurCacheLRU::invalidate(QStringView reason, bool skipGlContext) {
    if (m_entries.empty()) {
        return;
    }

    uint totalHits{0};
    for (auto &entry : m_entries) {
        totalHits += entry->hits;
    }

    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX
                        << "Invalidating cache:" << m_windowClass << "\n"
                        << "PID:" << m_windowPID << "\n"
                        << "Hits:" << totalHits << "across" << m_entries.size() << "cache entries" << "\n"
                        << "Reason:" << reason;

    // invalidate can be called from various events outside
    // the window paint pipeline so we need to explicitly
    // make the context current to correctly drop framebuffers/textures
    if (!skipGlContext) {
        KWin::effects->makeOpenGLContextCurrent();
    }

    m_entries.clear();
    reset();
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

void BBDX::BlurCache::preparePaintData(const KWin::Region *dirtyRegion,
                                       const KWin::GLFramebuffer *blitFramebuffer,
                                       const KWin::Rect *backgroundRect,
                                       const KWin::Rect *scaledBackgroundRect) {
    m_paintData.dirtyRegion = dirtyRegion;
    m_paintData.blitFramebuffer = blitFramebuffer;
    m_paintData.backgroundRect = backgroundRect;
    m_paintData.scaledBackgroundRect = scaledBackgroundRect;
    m_paintData.textureCompareRegion = KWin::Region{};

    for (const auto &rect : dirtyRegion->rects()) {
        m_paintData.textureCompareRegion |= rect.translated(-backgroundRect->topLeft());
    }
    m_paintData.textureCompareVertexCount = m_paintData.textureCompareRegion.rects().size() * 6;
}

void BBDX::BlurCache::selectCacheEntry(BBDX::BlurRenderData &renderInfo,
                                       KWin::GLVertexBuffer *vbo) {
    auto &cache = renderInfo.cache;
    cache.reset();

    KWin::GLTexture *blitTexture = renderInfo.framebuffers[0].get()->colorAttachment();

    // fast path in case we already determined we
    // can't perform texture comparison
    if (m_glQueryAvailable == GLQueryAvailable::NONE) [[unlikely]] {
        return;
    }

    while (auto cacheEntry = cache.next()) {
        // Somehow we can end up here with an empty textureCompareRegion
        // which would mean there was no dirtyRegion and thus no blitted data.
        // Just accept and bail.
        if (m_paintData.textureCompareRegion.isEmpty()) {
            cache.select(true);
            continue;
        }

        // Hijack FBO of the cached blit to avoid needless reallocation.
        // glColorMask should keep it protected
        const auto compareFramebuffer = cacheEntry->blitFramebuffer.get();

        // check if textures differ on the pixel level
        KWin::ShaderManager::instance()->pushShader(m_textureComparePass.shader.get());
        KWin::GLFramebuffer::pushFramebuffer(compareFramebuffer);

        QMatrix4x4 projectionMatrix;
        projectionMatrix.ortho(QRectF(0.0, 0.0, blitTexture->width(), blitTexture->height()));

        m_textureComparePass.shader->setUniform(m_textureComparePass.mvpMatrixLocation, projectionMatrix);

        m_textureComparePass.shader->setUniform(m_textureComparePass.texUnitOldLocation, 0);
        glActiveTexture(GL_TEXTURE0);
        cacheEntry->blitTexture->bind();

        m_textureComparePass.shader->setUniform(m_textureComparePass.texUnitNewLocation, 1);
        glActiveTexture(GL_TEXTURE1);
        blitTexture->bind();

        GLuint query;
        glGenQueries(1, &query);

        // don't acctually draw anything
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);

        // pick the first available query in preferred order (based on supposed speed)
        // https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBeginQuery.xhtml
        switch (m_glQueryAvailable) {
            case GLQueryAvailable::ANY_SAMPLES_PASSED_CONSERVATIVE:
                glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE, query);
                if (glGetError() == GL_NO_ERROR) [[likely]] {
                    break;
                }

                qCWarning(BLUR_CACHE) << "OpenGL error: GL_ANY_SAMPLES_PASSED_CONSERVATIVE query not available."
                                      << "Falling back to ANY_SAMPLES_PASSED.";
                m_glQueryAvailable = GLQueryAvailable::ANY_SAMPLES_PASSED;
                [[fallthrough]];

            case GLQueryAvailable::ANY_SAMPLES_PASSED:
                glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
                if (glGetError() == GL_NO_ERROR) [[likely]] {
                    break;
                }

                qCWarning(BLUR_CACHE) << "OpenGL error: GL_ANY_SAMPLES_PASSED query not available."
                                      << "Falling back to SAMPLES_PASSED.";
                m_glQueryAvailable = GLQueryAvailable::SAMPLES_PASSED;
                [[fallthrough]];

            case GLQueryAvailable::SAMPLES_PASSED:
                glBeginQuery(GL_SAMPLES_PASSED, query);
                if (glGetError() == GL_NO_ERROR) [[likely]] {
                    break;
                }

                qCWarning(BLUR_CACHE) << "OpenGL error: GL_SAMPLES_PASSED query not available."
                                      << "No more fallbacks.";
                m_glQueryAvailable = GLQueryAvailable::NONE;
                [[fallthrough]];

            [[unlikely]] default:
                goto cleanup;
        }

        {
            // we need scissoring to properly catch partial repaints,
            // else OpenGL can just draw everything even if the VBO says differently
            glEnable(GL_SCISSOR_TEST);

            const auto &rects = m_paintData.textureCompareRegion.rects();
            const uint vertsPerRect = vboCountTextureCompare() / rects.size();
            uint vboStart = vboStartTextureCompare();

            // draw each scissor region (Y-flipped; KWin topLeft maps to OpenGL bottomLeft)
            for (const auto &rect : rects) {
                // for the scissor round outwards
                const GLint left = rect.left();
                const GLint right = rect.right();
                const GLint top = compareFramebuffer->size().height() - rect.top();
                const GLint bottom = compareFramebuffer->size().height() - rect.bottom();

                const GLint width = right - left;
                const GLint height = top - bottom;

                // OpenGL requires these to be positive
                // and it wouldn't make sense to draw empty Rects anyway
                if (width <= 0 || height <= 0) {
                    vboStart += vertsPerRect;
                    continue;
                }

                glScissor(left, bottom, width, height);

                vbo->draw(GL_TRIANGLES, vboStart, vertsPerRect);
                vboStart += vertsPerRect;
            }

            glDisable(GL_SCISSOR_TEST);
        }

        switch (m_glQueryAvailable) {
            case GLQueryAvailable::ANY_SAMPLES_PASSED_CONSERVATIVE:
                glEndQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE);
                break;

            case GLQueryAvailable::ANY_SAMPLES_PASSED:
                glEndQuery(GL_ANY_SAMPLES_PASSED);
                break;

            case GLQueryAvailable::SAMPLES_PASSED:
                glEndQuery(GL_SAMPLES_PASSED);
                break;

            [[unlikely]] default:
                goto cleanup;
        }

        // await query and check, with a timeout because these
        // queries can be slow.
        // Reaching the timeout just selects the cache entry without verification
        // (if not partial; those will just be skipped)
        {
            // we always want to reach at least 60fps, so that's our timeout
            constexpr std::chrono::nanoseconds timeout{1000000000 / 60};

            // poll every ms
            constexpr std::chrono::nanoseconds pollRate{1000000000 / 1000};
            auto start = std::chrono::steady_clock::now();

            GLuint available{GL_FALSE};
            do {
                glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);

                auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
                if (!available && (elapsed > timeout)) {
                    qCWarning(BLUR_CACHE) << BBDX::LOG_PREFIX << "Aborting slow OpenGL query. Limit:" << timeout;
                    if (!cacheEntry->partial) {
                        cache.select();
                    }
                    goto cleanup;
                } else if (!available) {
                    std::this_thread::sleep_for(pollRate);
                }
            } while (!available);

            if (m_glQueryAvailable != GLQueryAvailable::SAMPLES_PASSED) [[likely]] {
                GLuint anyPixelsDifferent{GL_FALSE};
                glGetQueryObjectuiv(query, GL_QUERY_RESULT, &anyPixelsDifferent);
                if (anyPixelsDifferent == GL_FALSE) {
                    // no need to break; this causes BlurCacheLRU::next()
                    // to return nullptr on the next iteration
                    //
                    // this call also updates the verifiedAt timestamp
                    cache.select(true);
                }
            } else {
                GLuint pixelsDifferent{0};
                glGetQueryObjectuiv(query, GL_QUERY_RESULT, &pixelsDifferent);
                if (pixelsDifferent == 0) {
                    // no need to break; this causes BlurCacheLRU::next()
                    // to return nullptr on the next iteration
                    //
                    // this call also updates the verifiedAt timestamp
                    cache.select(true);
                } else {
                    // for debugging purposes also log the actual count
                    qCDebug(BLUR_CACHE) << BBDX::LOG_PREFIX << "Pixels different:" << pixelsDifferent;
                }
            }
        }

cleanup:
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDeleteQueries(1, &query);
        glActiveTexture(GL_TEXTURE0);

        KWin::GLFramebuffer::popFramebuffer();
        KWin::ShaderManager::instance()->popShader();
    }
}

void BBDX::BlurCache::selectCacheEntryEarly(BBDX::BlurRenderData &renderInfo) {
    auto &cache = renderInfo.cache;
    cache.reset();

    if (!(cache.window() && m_effect->windowManager()->windowIsBlurFullyCovered(cache.window()))) {
        return;
    }

    while (auto cacheEntry = cache.next()) {
        // partial entries shouldn't be reused without verification
        if (cacheEntry->partial) {
            continue;
        }

        cache.select();
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

void BBDX::BlurCache::drawCached(const KWin::Rect &scaledBackgroundRect, const KWin::RenderViewport &viewport, BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const {
    KWin::ShaderManager::instance()->pushShader(m_texturePass.shader.get());
    
    QMatrix4x4 projectionMatrix = viewport.projectionMatrix();
    projectionMatrix.translate(scaledBackgroundRect.x(), scaledBackgroundRect.y());

    KWin::GLTexture* read;
    if (const auto &cacheEntry = renderInfo.cache.valid()) {
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

void BBDX::BlurCache::drawToCache(BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo) const {
    auto cachedFramebuffer = renderInfo.cache.valid()->cachedFramebuffer.get();
    KWin::GLFramebuffer::pushFramebuffer(cachedFramebuffer);
    vbo->draw(GL_TRIANGLES, vboStartCache(), vboCountCache());
    KWin::GLFramebuffer::popFramebuffer();
}
