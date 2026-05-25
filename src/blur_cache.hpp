#pragma once

#include "kwin_version.hpp"

#include <epoxy/gl.h>

#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
#  include "kwin_compat_6_5.hpp"
#else
#  include <core/rect.h>
#endif

#include <chrono>
#include <memory>
#include <vector>

namespace KWin {
    class GLVertex2D;
}

namespace BBDX {
class BlurEffect;
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

    // priority index, lower meaning higher priority
    uint priority{0};

    // cache hits of this entry, incremented by BlurCacheLRU::select()
    uint hits{0};

    // last time this cache entry was verified; used for rate limiting
    std::chrono::time_point<std::chrono::steady_clock> verifiedAt{};

    // backgroundRect used to create this cache entry
    KWin::Rect backgroundRect{};

    // Marker for cache entries that are partial (didn't have full backgroundRect blitted).
    // Partial entries are fine to use for the regular "slow" path
    // but they can't be used for some performance hacks.
    bool partial{false};

    /**
     * Create a new BlurCacheEntry by allocating cachedTexture and cachedFramebuffer
     * blitTexture is cloned from the provided blitFramebuffer.
     */
    static std::unique_ptr<BlurCacheEntry> create(const KWin::Rect &scaledBackgroundRect,
                                                  BBDX::BlurCacheEntry *oldCacheEntry,
                                                  KWin::GLFramebuffer *dirtyBlitFramebuffer,
                                                  KWin::Region dirtyRegion,
                                                  KWin::Rect backgroundRect);

    /**
     * Update a BlurCacheEntry's blitTexture from the given dirtyBlitFramebuffer and dirtyRegion
     */
    void updateBlitTexture(KWin::GLFramebuffer *dirtyBlitFramebuffer, KWin::Region dirtyRegion);

    /**
     * Helper for mapping dirtyRegion into backgroundRect
     */
    KWin::Region localDirtyRegion(const KWin::Region &dirtyRegion) const;
};

/**
 * Least Recently Used container
 * for BlurCacheEntry
 */
class BlurCacheLRU {
private:
    std::unique_ptr<BlurCacheEntry> m_entry{};
    bool m_valid{false};

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
     * Select does the following:
     *  - acknowledge the current cache entry was a hit (makes valid() return true)
     *  - bump cache hits
     *  - if verified=true update the verifiedAt timestamp
     */
    void select(bool verified = false);

    /**
     * Reset select/valid state for next selection
     */
    void reset();

    /**
     * Add an entry to the cache, potentially removing the already existing entry.
     * The added entry is assumed to be valid by the time drawCached() is called
     * and will thus implicitly be selected.
     */
    void add(std::unique_ptr<BlurCacheEntry> entry);

    /**
     * Check if the cache entry was deemed valid
     */
    bool valid() { return m_valid; }

    /**
     * Explicitly clear remove the cache entry
     * and print sats to debug log
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
        int texUnitOldLocation;
        int texUnitNewLocation;
    } m_textureComparePass;

    struct {
        std::unique_ptr<KWin::GLShader> shader;
        int mvpMatrixLocation;
    } m_texturePass;

    // supported query types, in preferred/tried order
    enum class GLQueryAvailable {
        ANY_SAMPLES_PASSED_CONSERVATIVE,
        ANY_SAMPLES_PASSED,
        SAMPLES_PASSED,
        NONE,
    };

    // pointer to the managing effect
    BlurEffect *m_effect{nullptr};

    /**
     * set to the best supported query that
     * didn't return an error so far
     *
     * DEBUGGING: setting this to basic SAMPLES_PASSED enables pixel diff logging
     */
    GLQueryAvailable m_glQueryAvailable{GLQueryAvailable::ANY_SAMPLES_PASSED_CONSERVATIVE};

    /**
     * Reusable query object for texture comparison
     */
    GLuint m_queryObject{0};

    // Data used for this specific window paint
    // !!! preparePaintData() must be called before accessing any of this !!!
    struct {
        const KWin::Region *dirtyRegion;
        const KWin::Rect *backgroundRect;
        const KWin::Rect *scaledBackgroundRect;
        const KWin::GLFramebuffer *blitFramebuffer;

        // dirtyRegion adjusted for use in setupVBO()
        // and the vertexCount it will use
        KWin::Region textureCompareRegion{};
        uint textureCompareVertexCount;
    } m_paintData;

public:
    /**
     * Loads and sets up shaders
     */
    explicit BlurCache(BlurEffect *effect);

    ~BlurCache();

    /**
     * Check if pass is ready i.e. all shaders loaded
     */
    bool ready() const { return !!m_texturePass.shader; }

    /**
     * Prepare the cache for this paint
     */
    void preparePaintData(const KWin::Region *dirtyRegion,
                          const KWin::GLFramebuffer *blitFramebuffer,
                          const KWin::Rect *backgroundRect,
                          const KWin::Rect *scaledBackgroundRect);

    /**
     * Select a cache entry from renderInfo if a valid one exists
     * by comparing the underlying texture
     */
    void selectCacheEntry(BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo);

    /**
     * Select a cache entry from renderInfo if a valid one exists
     * by checking if a recent one exists
     */
    void selectCacheEntryEarly(BBDX::BlurRenderData &renderInfo);

    /**
     * Injects the geometry used for the cache, in logical pixels
     * but scaled to what would be drawn on the device.
     *
     * Adds BlurCache::addedVertices() vertices
     */
    void setupVBO(std::span<KWin::GLVertex2D> &map, size_t &vboIndex) const;
    uint addedVertices() const { return m_paintData.textureCompareVertexCount + 6; }

    /**
     * Start indices and vert count of stuff in the VBO
     */
    uint vboStartTextureCompare() const { return 6; }
    uint vboCountTextureCompare() const { return m_paintData.textureCompareVertexCount; }
    uint vboStartCache() const { return vboStartTextureCompare() + vboCountTextureCompare(); }
    uint vboCountCache() const { return 6; }
    uint vboStartScreen() const { return vboStartCache() + vboCountCache(); }

    /**
     * Draw the cached texture
     */
    void drawCached(const KWin::Rect &scaledBackgroundRect, const KWin::RenderViewport &viewport, BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const;

    /**
     * vbo->draw() wrapper to draw into BlurCacheData of the provided renderInfo
     */
    void drawToCache(BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo) const;
};

} // namespace BBDX
