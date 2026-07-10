#pragma once

#include "kwin_compat.hpp"

#include <opengl/gltexture.h>
#include <opengl/glframebuffer.h>

#include <epoxy/gl.h>

#include <QSize>
#include <QString>

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
QSize getTextureSize(const QRect &backgroundRect, const size_t i);

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
void setTextureSwizzle(KWin::GLTexture *texture);

/**
 * Enable GL_SCISSOR_TEST and set an appropriate
 * scissor rect for the given dirtyRegion, backgroundRect
 *
 * implicitly targets the current attached framebuffer and
 * thus must be called after GLFramebuffer::pushFramebuffer()
 */
void setGLScissor(const KWin::Region &dirtyRegion, const KWin::Rect &backgroundRect);

/**
 * Cleanup for setGLScissor
 *
 * should be cleared right before drawing on the screen
 */
void clearGLScissor();

/**
 * Compatibility helper for loading the proper shader files
 *
 * Plasma <6.7 always needed 2 versions of a shader in the QRC path
 * - "legacy" (no suffix) and "core" (_core suffix)
 * with the "core" version loaded in OpenGL 3.1+ environments
 *
 * Plasma >=6.7 only wants the "core" shader and downgrades it internally
 * by injecting helper funtions
 */
QString shaderFilePath(const char *path);

/**
 * Version agnostic roundedIn/roundedOut helper for RectF
 */
KWin::Rect rectRoundedIn(KWin::RectF rect);
KWin::Rect rectRoundedOut(KWin::RectF rect);

}
