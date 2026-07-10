#include "utils.h"

#include "kwin_compat.hpp"

#include <opengl/gltexture.h>
#include <opengl/glframebuffer.h>

#include <epoxy/gl.h>

#include <QSize>
#include <QLoggingCategory>
#include <qloggingcategory.h>

Q_LOGGING_CATEGORY(BBDX_UTILS, "kwin_effect_better_blur_dx.utils", QtInfoMsg)

QSize BBDX::getTextureSize(const QRect &backgroundRect, const size_t i) {
    return QSize(std::max(1, backgroundRect.width() / (1 << i)),
                 std::max(1, backgroundRect.height() / (1 << i)));
}

void BBDX::setTextureSwizzle(KWin::GLTexture *texture) {
    texture->setSwizzle(GL_RED, GL_GREEN, GL_BLUE, GL_ONE);
}

void BBDX::setGLScissor(const KWin::Region &dirtyRegion, const KWin::Rect &backgroundRect) {
    const auto fbo = KWin::GLFramebuffer::currentFramebuffer();
    if (!fbo) {
        qCWarning(BBDX_UTILS) << "BBDX::setGLScissor() called with no GLFramebuffer attached";
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

void BBDX::clearGLScissor() {
    glScissor(0, 0, 0, 0);
    glDisable(GL_SCISSOR_TEST);
}

QString BBDX::shaderFilePath(const char *path) {
    QString shader{path};

#if KWIN_VERSION >= KWIN_VERSION_CODE(6, 6, 90)
    shader.insert(shader.lastIndexOf("."), "_core");
#endif

    qCDebug(BBDX_UTILS) << "Loading shader file:" << shader;
    return shader;
}

KWin::Rect BBDX::rectRoundedIn(KWin::RectF rect) {
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
    return KWin::Rect(QPoint(std::ceil(rect.left()), std::ceil(rect.top())),
                      QPoint(std::floor(rect.right()), std::floor(rect.bottom())));
#else
    return rect.roundedIn();
#endif
}

KWin::Rect BBDX::rectRoundedOut(KWin::RectF rect) {
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
    return KWin::Rect(QPoint(std::floor(rect.left()), std::floor(rect.top())),
                      QPoint(std::ceil(rect.right()), std::ceil(rect.bottom())));
#else
    return rect.roundedIn();
#endif
}
