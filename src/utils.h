#pragma once

#include "effect/effectwindow.h"

#include <opengl/gltexture.h>

#include <QSize>

namespace BBDX
{

static const char LOG_PREFIX[]{"better_blur_dx:"};

inline bool isDockFloating(const KWin::EffectWindow *dock, const QRegion blurRegion)
{
    // If the pixel at (0, height / 2) for horizontal panels and (width / 2, 0) for vertical panels doesn't belong to
    // the blur region, the dock is most likely floating. The (0,0) pixel may be outside the blur region if the dock
    // can float but isn't at the moment.
    return !blurRegion.intersects(QRect(0, dock->height() / 2, 1, 1)) && !blurRegion.intersects(QRect(dock->width() / 2, 0, 1, 1));
}

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

}
