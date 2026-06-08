#pragma once

#include "kwin_compat.hpp"

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
#include <array>

namespace KWin {
    class GLVertex2D;
    class ScreenPrePaintData;
}

namespace BBDX {
class BlurEffect;
struct BlurRenderData;

/**
 * Cache invalidation type
 */
enum class BlurCacheInvalidation {
    // soft invalidation just marks cache dirty
    SOFT,
    // hard invalidation clears the cache entry entirely
    HARD,
};

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
    // Uses shared pointers because the validation query also gets
    // a handle to these
    std::shared_ptr<KWin::GLTexture> blitTexture{nullptr};
    std::shared_ptr<KWin::GLFramebuffer> blitFramebuffer{nullptr};

    // backgroundRect used to create this cache entry
    KWin::Rect backgroundRect{};

    /**
     * Create a new BlurCacheEntry by allocating cachedTexture and cachedFramebuffer
     * blitTexture is cloned from the provided blitFramebuffer.
     */
    static std::unique_ptr<BlurCacheEntry> create(const KWin::Rect &scaledBackgroundRect,
                                                  KWin::GLFramebuffer *dirtyBlitFramebuffer,
                                                  KWin::Region dirtyRegion,
                                                  KWin::Rect backgroundRect);

    /**
     * Update a BlurCacheEntry's blitTexture from the given dirtyBlitFramebuffer and dirtyRegion
     */
    void updateBlitTexture(KWin::GLFramebuffer *dirtyBlitFramebuffer, const KWin::Region &dirtyRegion);

    /**
     * Add dirtyRegion to accumulatedDirtyRegion
     */
    void accumulateDirtyRegion(const KWin::Region &dirtyRegion);

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
    KWin::EffectWindow* m_window{nullptr};
    QString m_windowClass{"unknown unknown"};
    pid_t m_windowPID{-1};

public:
    /**
     * Invalidate cache on destruction
     * (for stats and explicit OpenGL context)
     */
    ~BlurCacheLRU() {
        invalidate(BlurCacheInvalidation::HARD, QStringLiteral("BlurCacheLRU destroyed"));
    }

    /**
     * Return a pointer to the contained entry or nullptr
     * if none exists
     */
    BlurCacheEntry* get();

    /**
     * Add an entry to the cache, potentially removing the already existing entry.
     * The added entry is assumed to be valid by the time drawCached() is called
     * and will thus implicitly be selected.
     */
    void add(std::unique_ptr<BlurCacheEntry> entry);

    /**
     * Explicitly mark cache dirty (SOFT) or clear remove the cache entry (HARD)
     * and print sats to debug log
     *
     * By default this will make the OpenGL context current
     * before clearing the entry as this funtion may be called at any time.
     * Set skipGlContext in cases where the context is already current.
     */
    void invalidate(BlurCacheInvalidation type, QStringView reason, bool skipGlContext = false);

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

static constexpr size_t QUERY_OBJECT_COUNT{5};

class GLQueryObjectsDeleter {
public:
    void operator()(std::array<GLuint, QUERY_OBJECT_COUNT> *queryObjects) {
        if (queryObjects) {
            glDeleteQueries(queryObjects->size(), queryObjects->data());
        }
    }
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
     * Shared query objects across paints
     */
    std::unique_ptr<std::array<GLuint, QUERY_OBJECT_COUNT>, GLQueryObjectsDeleter> m_glQueryObjects{nullptr};
    size_t m_nextGlQueryObject{0};

    /**
     * set to the best supported query that
     * didn't return an error so far
     *
     * DEBUGGING: setting this to basic SAMPLES_PASSED enables pixel diff logging
     */
    GLQueryAvailable m_glQueryAvailable{GLQueryAvailable::ANY_SAMPLES_PASSED_CONSERVATIVE};

    // Data used for this specific window paint
    // !!! preparePaintData() must be called before accessing any of this !!!
    struct {
        const KWin::RenderView *view;
        const KWin::EffectWindow *window;
        const KWin::Region *dirtyRegion;
        const KWin::Rect *backgroundRect;
        const KWin::Rect *scaledBackgroundRect;
        KWin::GLFramebuffer *blitFramebuffer;

        // dirtyRegion adjusted for use in setupVBO()
        // and the vertexCount it will use
        KWin::Region textureCompareRegion{};
        uint textureCompareVertexCount;

        // on first paint before we have a usuable cache entry
        // we won't call glBeginConditionalRender
        bool glBeginConditionalRenderCalled{false};
    } m_paintData;

public:
    /**
     * Loads and sets up shaders
     */
    explicit BlurCache(BlurEffect *effect);

    /**
     * Check if pass is ready i.e. all shaders loaded
     */
    bool ready() const { return !!m_texturePass.shader; }

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
     * Set up query and call glBeginConditionalRender
     *
     * Should be called after VBO was bound
     * 
     * The regular blur passes should happen between this and
     * BlurCached::rawCached()
     */
    void prepareCache(BlurCacheLRU &cache, KWin::GLVertexBuffer *vbo);

    /**
     * Call glEndConditionalRender and draw the cached texture
     *
     * Should be called at the very end of the blur passes
     */
    void drawCached(const KWin::Rect &scaledBackgroundRect, const KWin::RenderViewport &viewport, BBDX::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const;

    /**
     * vbo->draw() wrapper to draw into BlurCacheData of the provided cache
     */
    void drawToCache(BBDX::BlurCacheLRU &cache, KWin::GLVertexBuffer *vbo) const;
};

} // namespace BBDX
