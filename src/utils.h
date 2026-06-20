#pragma once

#include <opengl/gltexture.h>
#include <opengl/glframebuffer.h>

#include <epoxy/gl.h>

#include <QSize>

namespace BBDX
{

static const char LOG_PREFIX[]{"better_blur_dx:"};

/**
 * Get texture size for offscreen framebuffer allocation during BlurEffect::blur()
 * Scaled down by 2^i
 *
 * For very small windows, the width and/or height of the last blur texture may be 0. Creation of
 * and/or usage of invalid textures to create framebuffers appears to cause performance issues.
 * https://github.com/taj-ny/kwin-effects-forceblur/issues/160
 */
inline QSize getTextureSize(const QRect &backgroundRect, const size_t i) {
    return QSize(std::max(1, backgroundRect.width() / (1 << i)),
                 std::max(1, backgroundRect.height() / (1 << i)));
}

/**
 * When reading textures make the alpha channel a constant 1.
 *
 * At screen edges KWin seems to give us textures where (I'm assuming)
 * all RGBA values are 0.
 * This results in weird blur artifacts around screen edges.
 *
 * This workaround sort of replaces the artifacts with a dark gradient, which
 * technically isn't correct either but better than looking completely broken.
 */
inline void setTextureSwizzle(KWin::GLTexture *texture) {
    texture->setSwizzle(GL_RED, GL_GREEN, GL_BLUE, GL_ONE);
}

/**
 * Enable GL_SCISSOR_TEST and set an appropriate
 * scissor rect for the given dirtyRegion, backgroundRect
 *
 * implicitly targets the current attached framebuffer and
 * thus must be called after GLFramebuffer::pushFramebuffer()
 */
inline void setGLScissor(const KWin::Region &dirtyRegion, const KWin::Rect &backgroundRect) {
    const auto fbo = KWin::GLFramebuffer::currentFramebuffer();
    if (!fbo) {
        return;
    }

    const auto texture = fbo->colorAttachment();

    const double scaleX{static_cast<double>(texture->width()) / static_cast<double>(backgroundRect.width())};
    const double scaleY{static_cast<double>(texture->height()) / static_cast<double>(backgroundRect.height())};

    // slight expansion to ensure we don't cut of edges
    const KWin::RectF boundingRect{dirtyRegion.translated(-backgroundRect.topLeft()).boundingRect().adjusted(-8, -8, 8, 8)};

    const double scaledLeft{std::max(0.0, std::floor(boundingRect.left() * scaleX))};
    const double scaledTop{std::max(0.0, std::floor(boundingRect.top() * scaleY))};
    const double scaledRight{std::min(static_cast<double>(texture->width()), std::ceil(boundingRect.right() * scaleX))};
    const double scaledBottom{std::min(static_cast<double>(texture->height()), std::ceil(boundingRect.bottom() * scaleY))};

    const int glWidth{std::max(0, static_cast<int>(scaledRight - scaledLeft))};
    const int glHeight{std::max(0, static_cast<int>(scaledBottom - scaledTop))};

    const int glX{static_cast<int>(scaledLeft)};
    const int glY{static_cast<int>(texture->height() - (scaledTop + glHeight))};

    glEnable(GL_SCISSOR_TEST);
    glScissor(glX, glY, glWidth, glHeight);
}

/**
 * Cleanup for setGLScissor
 *
 * should be cleared right before drawing on the screen
 */
inline void clearGLScissor() {
    glScissor(0, 0, 0, 0);
    glDisable(GL_SCISSOR_TEST);
}

}
