#include "rounded_corners_pass.hpp"
#include "utils.h"
#include "blur.h"

#include <core/pixelgrid.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effectwindow.h>
#include <epoxy/gl_generated.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <scene/borderradius.h>

#include <QLoggingCategory>
#include <QRect>
#include <QImage>


Q_LOGGING_CATEGORY(ROUNDED_CORNERS_PASS, "kwin_effect_better_blur_dx.rounded_corners_pass", QtWarningMsg)

BBDX::RoundedCornersPass::RoundedCornersPass() {
    m_shader = KWin::ShaderManager::instance()->generateShaderFromFile(
            KWin::ShaderTrait::MapTexture,
            QStringLiteral(":/effects/better_blur_dx/shaders/rounded_corners.vert"),
            QStringLiteral(":/effects/better_blur_dx/shaders/rounded_corners.frag"));

    if (!m_shader) {
        qCWarning(ROUNDED_CORNERS_PASS) << BBDX::LOG_PREFIX << "Failed to load rounded corners pass shader";
        return;
    } else {
        m_mvpMatrixLocation = m_shader->uniformLocation("modelViewProjectionMatrix");
        m_boxLocation = m_shader->uniformLocation("box");
        m_cornerRadiusLocation = m_shader->uniformLocation("cornerRadius");
    }
}

void BBDX::RoundedCornersPass::apply(const KWin::BorderRadius &cornerRadius,
                                     const KWin::RenderViewport &viewport,
                                     const QRect &scaledBackgroundRect,
                                     const KWin::BlurRenderData &renderInfo,
                                     const KWin::EffectWindow *w,
                                     const KWin::WindowPaintData &data,
                                     KWin::GLVertexBuffer *vbo,
                                     const int vertexCount) const {
        if (!ready()) [[unlikely]] {
            return;
        }

        KWin::ShaderManager::instance()->pushShader(m_shader.get());

        QMatrix4x4 projectionMatrix = viewport.projectionMatrix();
        projectionMatrix.translate(scaledBackgroundRect.x(), scaledBackgroundRect.y());

        // should contain the raw un-blurred pixels
        const auto &read = renderInfo.framebuffers[0];

        const QRectF transformedRect = QRectF{
            w->frameGeometry().x() + data.xTranslation(),
            w->frameGeometry().y() + data.yTranslation(),
            w->frameGeometry().width() * data.xScale(),
            w->frameGeometry().height() * data.yScale(),
        };
        const QRectF nativeBox = KWin::snapToPixelGridF(KWin::scaledRect(transformedRect, viewport.scale()))
                                     .translated(-scaledBackgroundRect.topLeft());
        const KWin::BorderRadius nativeCornerRadius = cornerRadius.scaled(viewport.scale()).rounded();

        m_shader->setUniform(m_mvpMatrixLocation, projectionMatrix);
        m_shader->setUniform(m_boxLocation, QVector4D(nativeBox.x() + nativeBox.width() * 0.5, nativeBox.y() + nativeBox.height() * 0.5, nativeBox.width() * 0.5, nativeBox.height() * 0.5));
        m_shader->setUniform(m_cornerRadiusLocation, nativeCornerRadius.toVector());

        BBDX::setTextureSwizzle(read->colorAttachment());
        read->colorAttachment()->bind();

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        vbo->draw(GL_TRIANGLES, 6, vertexCount);

        glDisable(GL_BLEND);

        KWin::ShaderManager::instance()->popShader();
}
