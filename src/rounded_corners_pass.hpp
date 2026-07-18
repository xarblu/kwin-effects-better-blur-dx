#pragma once

#include "kwin_compat.hpp"
#include "window_manager.hpp"

#include <opengl/glshader.h>

#include <QMatrix4x4>
#include <QVector2D>
#include <QVector4D>
#include <QtNumeric>

#include <opengl/glvertexbuffer.h>

#include <memory>

namespace KWin {
    class BorderRadius;
    class EffectWindow;
    class GLVertexBuffer;
    class RenderViewport;
    class WindowPaintData;
}

namespace BBDX {
    class BlurCache;
    class BlurCacheEntry;
}

namespace BBDX {
struct BlurRenderData;

class RoundedCornersPass {
private:
    std::unique_ptr<KWin::GLShader> m_shader{nullptr};
    int m_mvpMatrixLocation;
    int m_boxLocation;
    int m_cornerRadiusLocation;

    RoundedCornersPass() = default;

public:
    /**
     * Loads required shaders and sets up shader uniformLocations
     * nullptr on error
     */
    static std::unique_ptr<RoundedCornersPass> create();

    /**
     * Apply rounded corners by setting their alpha channel to 0.0
     *
     * and set texture swizzle accordingly
     * (rounded -> alpha=alpha; square -> alpha=1.0)
     */
    void apply(const BBDX::WindowManager *windowManager,
               const KWin::Rect &backgroundRect,
               const KWin::EffectWindow *w,
               const KWin::WindowPaintData &data,
               KWin::GLVertexBuffer *vbo,
               const BBDX::BlurCache *blurCache,
               BBDX::BlurCacheEntry *cacheEntry) const;
};

} // namespace BBDX

