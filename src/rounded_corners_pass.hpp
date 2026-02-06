#pragma once

#include <opengl/glshader.h>

#include <QMatrix4x4>
#include <QVector2D>
#include <QVector4D>
#include <QtNumeric>

#include <opengl/glvertexbuffer.h>

namespace KWin {
    class BorderRadius;
    class EffectWindow;
    class GLVertexBuffer;
    class RenderViewport;
    class WindowPaintData;
    struct BlurRenderData;
}

namespace BBDX {

class RoundedCornersPass {
private:
    std::unique_ptr<KWin::GLShader> m_shader{nullptr};
    int m_mvpMatrixLocation;
    int m_boxLocation;
    int m_cornerRadiusLocation;

public:
    /**
     * Loads required shaders and sets up shader uniformLocations
     */
    explicit RoundedCornersPass();

    /**
     * Check if pass is ready i.e. all shaders loaded
     */
    bool ready() const { return m_shader != nullptr; }

    /**
     * Apply rounded corners with a mask from renderInfo.framebuffer[0]
     * which should contain the raw un-blurred pixels of the blur area.
     */
    void apply(const KWin::BorderRadius &cornerRadius,
               const KWin::RenderViewport &viewport,
               const QRect &scaledBackgroundRect,
               const KWin::BlurRenderData &renderInfo,
               const KWin::EffectWindow *w,
               const KWin::WindowPaintData &data,
               KWin::GLVertexBuffer *vbo,
               const int vertexCount) const;
};

} // namespace BBDX

