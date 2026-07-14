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

    // 1 <= scissor width/height <= texture width/height
    const int glWidth{std::min(std::max(static_cast<int>(std::ceil(scaledRight - scaledLeft)), 1), texture->width())};
    const int glHeight{std::min(std::max(static_cast<int>(std::ceil(scaledBottom - scaledTop)), 1), texture->height())};

    // 0 <= scissor x/y
    int glX{std::max(static_cast<int>(scaledLeft), 0)};
    int glY{std::max(static_cast<int>(texture->height() - (scaledTop + glHeight)), 0)};

    // move box towards (0,0) in case it doesn't fit
    if (glX + glWidth > texture->width()) {
        glX = texture->width() - glWidth;
    }
    if (glY + glHeight > texture->height()) {
        glY = texture->height() - glHeight;
    }

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

KWin::RegionF BBDX::regionTranslatedF(KWin::RegionF region, QPointF translation) {
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
    // < 6.7 maps RegionF to integral (Q)Region
    return region.translated(translation.toPoint());
#else
    return region.translated(translation);
#endif
}
