#pragma once

#include "kwin_compat.hpp"
#include "settings.hpp"

#include <chrono>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <epoxy/gl.h>

#include <QObject>

#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>
#include <scene/scene.h>
#include <unordered_map>

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
struct BlurRenderData;

// options for cache invalidation mask
enum class BlurCacheInvalidationFlag : uint {
    /**
     * mark cache completely invalid
     * causing a drop and subsequent creation of fresh entry
     */
    FULL = 1 << 0,

    /**
     * only clear cachedRegion
     * causing that area to be flushed
     */
    REGION = 1 << 1,
};

/**
 * A single valid entry
 */
class BlurCacheEntry {
    // texture and framebuffer for the cache
    // with the size of backgroundRect from BlurEffect::blur()
    std::unique_ptr<KWin::GLTexture> m_cachedTexture{nullptr};
    std::unique_ptr<KWin::GLFramebuffer> m_cachedFramebuffer{nullptr};

    /**
     * Region that has cached data
     * Updated by flushed()
     */
    KWin::Region m_cachedRegion{};

    /**
     * backgroundRect behind this cache entry
     * updated by BlurCache::preparePaintData()
     */
    KWin::Rect m_backgroundRect{};

    /**
     * dirtyRegion accumulated since lastFlush
     * in global coordinated
     * TODO: throw away on geometry change?
     * updated by BlurCache::preparePaintData()
     */
    KWin::Region m_accumulatedDirtyRegion{};
    std::chrono::steady_clock::time_point m_lastFlush{};

    /**
     * true if accumulatedDirtyRegion was consumed in prePaintScreen
     * and we need to to check and potentially re-blur
     *
     * A new cache entry should always flush immediately
     */
    bool m_isFlushing{true};

    /**
     * The cache is always flushed until this is exceeded
     * mostly to ensure animations finish properly
     */
    std::chrono::steady_clock::time_point m_flushingUntil{};

    /**
     * Marks this cache entry invalid (purging it the next paint cycle)
     *
     * This should be used when handling Qt eventloop stuff as the GL context
     * might not be current there.
     *
     * If you know the GL context is current feel free to just .reset() the unique_ptr
     */
    bool m_valid{true};

    /**
     * Some metadata to print on invalidation
     */
    QString m_windowClass{"unknown unknown"};
    pid_t m_windowPID{-1};

    /**
     * Use create()
     */
    BlurCacheEntry() = default;

public:
    /**
     * Create a new BlurCacheEntry by allocating cachedTexture and cachedFramebuffer
     * with the size of backgroundRect and format of dirtyBlitFramebuffer
     *
     * The limiting factor in terms of quality definitely is the blit itself anyways
     * (logical un-scaled pixels) so un-scaled backgroundRect should be sufficient
     */
    static std::unique_ptr<BlurCacheEntry> create(const KWin::Rect &backgroundRect,
                                                  GLenum internalFormat,
                                                  const KWin::EffectWindow *window);

    /**
     * Disallow copying GL resources
     */
    BlurCacheEntry(BlurCacheEntry &other) = delete;
    BlurCacheEntry& operator=(BlurCacheEntry &other) = delete;

    /**
     * Check if the dirtyRegion is fully cached
     */
    bool hasCachedRegion(const KWin::Region &dirtyRegion) const;

    /**
     * Add dirtyRegion to accumulatedDirtyRegion
     */
    void accumulateDirtyRegion(const KWin::Region &dirtyRegion);

    /**
     * Mark this entry for flushing
     *
     * While flushing abort with abortFlush() or complete with flushed()
     */
    void flush(const char *msg = nullptr);
    void abortFlush(const char *msg = nullptr);
    void flushed(const KWin::Region &dirtyRegion);

    /**
     * Like flush() but keeps the flush alive for
     * the given duration (mostly to ensure animations complete)
     */
    void flushFor(std::chrono::milliseconds duration, const char *msg = nullptr);

    /**
     * Extend flush while flushFor() duration is not elapsed
     */
    void maybeExtendFlush();

    /**
     * Invalidate cache entry
     *
     * Flags indicate what should be invalidated
     */
    void invalidate(uint flags = static_cast<uint>(BlurCacheInvalidationFlag::FULL), const char *msg = nullptr);

    /**
     * Setters
     */
    void setBackgroundRect(const KWin::Rect &rect) { m_backgroundRect = rect; }

    /**
     * Getters
     */
    KWin::GLTexture* cachedTexture() const { return m_cachedTexture.get(); }
    KWin::GLFramebuffer* cachedFramebuffer() const { return m_cachedFramebuffer.get(); }
    const KWin::Region& accumulatedDirtyRegion() const { return m_accumulatedDirtyRegion; }
    const std::chrono::steady_clock::time_point& lastFlush() const { return m_lastFlush; }
    bool isFlushing() const { return m_isFlushing; }
    bool valid() const { return m_valid; }
};

struct BlurCachePaintData {
    const KWin::RenderTarget *renderTarget;
    const KWin::RenderViewport *viewport;
    const KWin::RenderView *view;
    const KWin::EffectWindow *window;
    const KWin::Region *dirtyRegion;
    const KWin::Rect *backgroundRect;
    const KWin::Rect *scaledBackgroundRect;
    KWin::GLFramebuffer *blitFramebuffer;
};

struct WallpaperData {
    KWin::RectF geometry;
    std::unique_ptr<KWin::GLFramebuffer> framebuffer;
    std::unique_ptr<KWin::GLTexture> texture;

    // underlying window and whether it was marked damaged
    KWin::Window *window{};
    bool damaged{false};

    // connection to the underlying desktop window's damaged signal
    QMetaObject::Connection connection;
};

class BlurCache : public QObject {
    Q_OBJECT
private:
    struct {
        std::unique_ptr<KWin::GLShader> shader;
        int mvpMatrixLocation;
        int modulationLocation;
    } m_texturePass;

    // pointer to the managing effect
    BlurEffect *m_effect{nullptr};

    // Data used for this specific window paint
    // !!! preparePaintData() must be called before accessing any of this !!!
    BlurCachePaintData m_paintData{};

    /**
     * Wallpaper buffers for wallpaper mode
     */
    std::unordered_map<KWin::RenderView *, WallpaperData> m_wallpapers{};

    /**
     * User settings
     */
    BlitMode m_blitMode{BlitMode::RENDER_TARGET};
    bool m_ignoreCache{false};
    int m_cacheRateLimit{0};

    /**
     * use create()
     */
    BlurCache() = default;

public Q_SLOTS:
    /**
     * Called whenever a wallpaper window marks itself damaged
     */
    void slotWallpaperDamaged(KWin::Window *window);

public:
    /**
     * Loads and sets up shaders
     * nullptr on error
     */
    static std::unique_ptr<BlurCache> create(BlurEffect *effect);

    /**
     * reconfigure() hook
     */
    void reconfigure();

    /**
     * Getters
     */
    BlitMode blitMode() const { return m_blitMode; }
    bool ignoreCache() const { return m_ignoreCache; }
    int cacheRateLimit() const { return m_cacheRateLimit; }

    /**
     * Prepare the cache for this paint
     * and create an entry in the given cache unique_ptr if
     * one doesn't exist already
     */
    void preparePaintData(const KWin::RenderTarget *renderTarget,
                          const KWin::RenderViewport *viewport,
                          const KWin::RenderView *view,
                          const KWin::EffectWindow *window,
                          const KWin::Region *dirtyRegion,
                          KWin::GLFramebuffer *blitFramebuffer,
                          const KWin::Rect *backgroundRect,
                          const KWin::Rect *scaledBackgroundRect,
                          std::unique_ptr<BlurCacheEntry> &cache);

    /**
     * Injects the geometry used for the cache, in logical pixels
     * but scaled to what would be drawn on the device.
     *
     * Adds BlurCache::addedVertices() vertices
     */
    uint addedVertices() const;
    void setupVBO(std::span<KWin::GLVertex2D> &map, size_t &vboIndex) const;

    /**
     * Start indices and vert count of stuff in the VBO
     */
    uint vboStartCache() const { return 6; }
    uint vboCountCache() const { return addedVertices(); }
    uint vboStartScreen() const { return vboStartCache() + vboCountCache(); }

    /**
     * Call draw the cached texture
     *
     * Should be called at the very end of the blur passes
     */
    void drawCached(const KWin::RenderViewport &viewport, BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const;

    /**
     * vbo->draw() wrapper to draw into BlurCacheData of the provided cache
     */
    void drawToCache(BBDX::BlurCacheEntry *cache, KWin::GLVertexBuffer *vbo) const;

    /**
     * Flush all window's accumulatedDirtyRegions
     */
    void flushAccumulatedDirtyRegions(KWin::ScreenPrePaintData &data) const;

    /**
     * Get the wallpaper buffer+texture for the current paintData
     *
     * nullptr on error
     */
    WallpaperData* getWallpaper();

    /**
     * Drop wallpaper e.g. when the view was removed
     *
     * Ensures the OpenGL context is current
     */
    void dropWallpaper(KWin::RenderView *view);
};

} // namespace BBDX
