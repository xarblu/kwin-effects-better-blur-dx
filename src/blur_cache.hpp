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
class BlurCacheEntry {
    // texture and framebuffer for the cache
    // with the size of scaledBackgroundRect from BlurEffect::blur()
    std::unique_ptr<KWin::GLTexture> m_cachedTexture{nullptr};
    std::unique_ptr<KWin::GLFramebuffer> m_cachedFramebuffer{nullptr};

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
     * Use create()
     */
    BlurCacheEntry() = default;

public:
    /**
     * Create a new BlurCacheEntry by allocating cachedTexture and cachedFramebuffer
     * with the size of scaledBackgroundRect and format of dirtyBlitFramebuffer
     */
    static std::unique_ptr<BlurCacheEntry> create(const KWin::Rect &scaledBackgroundRect,
                                                  const KWin::GLFramebuffer *dirtyBlitFramebuffer);

    /**
     * Add dirtyRegion to accumulatedDirtyRegion
     */
    void accumulateDirtyRegion(const KWin::Region &dirtyRegion);

    /**
     * Helpers for mapping dirtyRegion into backgroundRect
     */
    KWin::Region localDirtyRegion(const KWin::Region &dirtyRegion) const;

    /**
     * Mark this entry for flushing
     *
     * While flushing abort with abortFlush() or complete with flushed()
     */
    void flush();
    void abortFlush(const char* msg = nullptr);
    void flushed();

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
    BlurCachePaintData m_paintData{};

    /**
     * use create()
     */
    BlurCache() = default;

public:
    /**
     * Loads and sets up shaders
     * nullptr on error
     */
    static std::unique_ptr<BlurCache> create(BlurEffect *effect);

    /**
     * Prepare the cache for this paint
     */
    void preparePaintData(const KWin::RenderTarget *renderTarget,
                          const KWin::RenderViewport *viewport,
                          const KWin::RenderView *view,
                          const KWin::EffectWindow *window,
                          const KWin::Region *dirtyRegion,
                          KWin::GLFramebuffer *blitFramebuffer,
                          const KWin::Rect *backgroundRect,
                          const KWin::Rect *scaledBackgroundRect,
                          BlurCacheLRU &cache);

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
     * Call draw the cached texture
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
