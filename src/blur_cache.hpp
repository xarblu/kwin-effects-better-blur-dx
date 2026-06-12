#pragma once

#include "kwin_compat.hpp"
#include "texture_comparer.hpp"

#include <chrono>
#include <effect/effect.h>
#include <epoxy/gl.h>

#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>
#include <scene/scene.h>

#if KWIN_VERSION >= KWIN_VERSION_CODE(6, 5, 80)
#  include <core/rect.h>
#endif

#include <memory>

namespace KWin {
    class GLVertex2D;
    class ScreenPrePaintData;
}

namespace BBDX {
class BlurEffect;
class TextureComparer;
struct BlurRenderData;


/**
 * A single valid entry
 */
struct BlurCacheEntry {
    // texture and framebuffer for the cache
    // with the size of scaledBackgroundRect from BlurEffect::blur()
    std::unique_ptr<KWin::GLTexture> cachedTexture{nullptr};
    std::unique_ptr<KWin::GLFramebuffer> cachedFramebuffer{nullptr};

    // texture of the previous "raw" pixels (blit grabbed from scene)
    // used to create this cache entry
    std::unique_ptr<KWin::GLTexture> blitTexture{nullptr};
    std::unique_ptr<KWin::GLFramebuffer> blitFramebuffer{nullptr};

    /**
     * backgroundRect behind this cache entry
     * updated by BlurCache::preparePaintData()
     */
    KWin::Rect backgroundRect{};

    /**
     * dirtyRegion accumulated since lastFlush
     * in global coordinated
     * TODO: throw away on geometry change?
     * updated by BlurCache::preparePaintData()
     */
    KWin::Region accumulatedDirtyRegion{};
    std::chrono::steady_clock::time_point lastFlush{};

    /**
     * true if accumulatedDirtyRegion was consumed in prePaintScreen
     * and we need to to check and potentially re-blur
     *
     * A new cache entry should always flush immediately
     */
    bool isFlushing{true};

    /**
     * Create a new BlurCacheEntry by allocating cachedTexture and cachedFramebuffer
     * blitTexture is cloned from the provided blitFramebuffer.
     */
    static std::unique_ptr<BlurCacheEntry> create(const KWin::Rect &scaledBackgroundRect,
                                                  KWin::GLFramebuffer *dirtyBlitFramebuffer,
                                                  KWin::Region dirtyRegion,
                                                  KWin::Rect backgroundRect);

    /**
     * Add dirtyRegion to accumulatedDirtyRegion
     */
    void accumulateDirtyRegion(const KWin::Region &dirtyRegion);

    /**
     * Helpers for mapping dirtyRegion into backgroundRect
     *
     * the *GL version additionally flips along the y axis into OpenGL coordinates
     */
    KWin::Region localDirtyRegion(const KWin::Region &dirtyRegion) const;
    KWin::Region localDirtyRegionGL(const KWin::Region &dirtyRegion) const;

    /**
     * Mark this entry for flushing and reset accumulatedDirtyRegion
     */
    void flush();
    void flushed();
};

/**
 * Least Recently Used container
 * for BlurCacheEntry
 */
class BlurCacheLRU {
private:
    std::unique_ptr<BlurCacheEntry> m_entry{};
    std::unique_ptr<TextureComparer::WindowData> m_textureCompareWindowData{};
    KWin::EffectWindow* m_window{nullptr};
    QString m_windowClass{"unknown unknown"};
    pid_t m_windowPID{-1};

public:
    /**
     * Invalidate cache on destruction
     * (for stats and explicit OpenGL context)
     */
    ~BlurCacheLRU() {
        invalidate(QStringLiteral("BlurCacheLRU destroyed"));
    }

    /**
     * Return a pointer to the contained entry or nullptr
     * if none exists
     */
    BlurCacheEntry* get();

    /**
     * Return const pointer to the contained texture compare
     * region and lazily create it if needed
     */
    TextureComparer::WindowData* textureCompareWindowData();

    /**
     * Add an entry to the cache, potentially removing the already existing entry.
     * The added entry is assumed to be valid by the time drawCached() is called
     * and will thus implicitly be selected.
     */
    void add(std::unique_ptr<BlurCacheEntry> entry);

    /**
     * Remove the cache entry and print sats to debug log
     *
     * By default this will make the OpenGL context current
     * before clearing the entry as this funtion may be called at any time.
     * Set skipGlContext in cases where the context is already current.
     */
    void invalidate(QStringView reason, bool skipGlContext = false);

    /**
     * Set window using this cache for logging purposes
     * Locked once set
     */
    void setWindow(KWin::EffectWindow* w);

    /**
     * Get pointer to the window if set
     */
    KWin::EffectWindow* window() const { return m_window; }
};

class BlurCache {
private:
    struct {
        std::unique_ptr<KWin::GLShader> shader;
        int mvpMatrixLocation;
    } m_texturePass;

    // pointer to the managing effect
    BlurEffect *m_effect{nullptr};

    // owned TextureComparer
    std::unique_ptr<TextureComparer> m_textureComparer{};

    // Data used for this specific window paint
    // !!! preparePaintData() must be called before accessing any of this !!!
    struct {
        const KWin::RenderView *view;
        const KWin::EffectWindow *window;
        const KWin::Region *dirtyRegion;
        const KWin::Rect *backgroundRect;
        const KWin::Rect *scaledBackgroundRect;
        KWin::GLFramebuffer *blitFramebuffer;

        // on first paint before we have a usuable cache entry
        // we won't call glBeginConditionalRender
        bool glBeginConditionalRenderCalled{false};

        // nothing inside backgroundRect is repainted this paint
        // but we have a valid cache entry - there is no fresh data
        // to compare against or blur so just draw the cached texture
        bool useCachedOnly{false};
    } m_paintData;

public:
    /**
     * Loads and sets up shaders
     */
    explicit BlurCache(BlurEffect *effect);

    /**
     * Check if pass is ready i.e. all shaders loaded
     */
    bool ready() const { return m_texturePass.shader && m_textureComparer; }

    /**
     * Prepare the cache for this paint
     */
    void preparePaintData(const KWin::RenderView *view,
                          const KWin::EffectWindow *window,
                          const KWin::Region *dirtyRegion,
                          KWin::GLFramebuffer *blitFramebuffer,
                          const KWin::Rect *backgroundRect,
                          const KWin::Rect *scaledBackgroundRect,
                          BlurCacheLRU &cache);

    /**
     * Whether this paint should skip the blit and blur passes
     * and only draw the existing cached texture.
     *
     * True when the dirtyRegion doesn't intersect backgroundRect
     * while a valid cache entry exists. Without fresh pixels there
     * is nothing to compare against or blur - rebuilding anyway would
     * recreate the cache from stale data.
     *
     * Only valid after preparePaintData() was called.
     */
    bool useCachedOnly() const { return m_paintData.useCachedOnly; }

    /**
     * Injects the geometry used for the cache, in logical pixels
     * but scaled to what would be drawn on the device.
     *
     * Adds BlurCache::addedVertices() vertices
     */
    void setupVBO(std::span<KWin::GLVertex2D> &map, size_t &vboIndex) const;
    uint addedVertices() const { return 6; }

    /**
     * Start indices and vert count of stuff in the VBO
     */
    uint vboStartCache() const { return 6; }
    uint vboCountCache() const { return 6; }
    uint vboStartScreen() const { return vboStartCache() + vboCountCache(); }

    /**
     * Set up query and call glBeginConditionalRender
     *
     * Should be called after VBO was bound
     *
     * The regular blur passes should happen between this and
     * BlurCached::rawCached()
     */
    void prepareCache(BlurCacheLRU &cache);

    /**
     * Call glEndConditionalRender and draw the cached texture
     *
     * Should be called at the very end of the blur passes
     */
    void drawCached(const KWin::RenderViewport &viewport, BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const;

    /**
     * vbo->draw() wrapper to draw into BlurCacheData of the provided cache
     */
    void drawToCache(BBDX::BlurCacheLRU &cache, KWin::GLVertexBuffer *vbo) const;

    /**
     * Flush all window's accumulatedDirtyRegions
     */
    void flushAccumulatedDirtyRegions(KWin::ScreenPrePaintData &data) const;
};

} // namespace BBDX
