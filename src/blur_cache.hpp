#pragma once

#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glvertexbuffer.h>

#include <memory>
#include <optional>

namespace KWin {
    struct BlurRenderData;
    class Rect;
    class GLVertex2D;
}

namespace BBDX {

/**
 * Blur cache data unique to each EffectWindow and RenderView combination
 */
struct BlurCacheData {
    // whether the cache is valid or not
    bool valid{false};

    // cache hits
    uint hits{0};

    // texture and framebuffer for the cache
    // with the size of scaledBackgroundRect from BlurEffect::blur()
    std::unique_ptr<KWin::GLTexture> texture;
    std::unique_ptr<KWin::GLFramebuffer> framebuffer;

    // things that affect validity of the cache
    std::optional<qreal> opacity{};

    // helper to invalidate cache, reset the hit counter
    // and print debug stats
    void invalidate();
};

class BlurCache {
private:
    struct {
        std::unique_ptr<KWin::GLShader> shader;
        int mvpMatrixLocation;
    } m_texturePass;

public:
    /**
     * Loads and sets up shaders
     */
    explicit BlurCache();

    /**
     * Check if pass is ready i.e. all shaders loaded
     */
    bool ready() const { return !!m_texturePass.shader; }

    /**
     * Updates the BlurCacheData buffers of the given renderInfo
     */
    void updateBlurCacheDataBuffers(KWin::BlurRenderData &renderInfo, const KWin::Rect &scaledBackgroundRect, GLenum textureFormat) const;

    /**
     * Update relevant properties and invalidate cache of provided renderInfo if they changed
     */
    void maybeInvalidateCache(BlurCacheData &cacheData, qreal opacity) const;

    /**
     * Injects the geometry used for the cache, in logical pixels
     * but scaled to what would be drawn on the device.
     *
     * Always adds 6 vertices
     */
    void setupVBO(const KWin::Rect &scaledBackgroundRect, std::span<KWin::GLVertex2D> &map, size_t &vboIndex) const;

    /**
     * Draw the cached texture
     */
    void drawCached(const KWin::Rect &scaledBackgroundRect, const KWin::RenderViewport &viewport, KWin::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo, const int vertexCount, const float modulation) const;

    /**
     * vbo->draw() wrapper to draw into BlurCacheData of the provided renderInfo
     */
    void drawToCache(const KWin::BlurRenderData &renderInfo, KWin::GLVertexBuffer *vbo) const;
};

} // namespace BBDX
