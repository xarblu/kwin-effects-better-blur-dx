#include "kwin_version.hpp"
#include "utils.h"
#include "window.hpp"
#include "window_manager.hpp"

#include <KDecoration3/Decoration>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <effect/globals.h>
#include <scene/borderradius.h>
#include <window.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80) || defined(BETTERBLUR_X11)
#  include "kwin_compat_6_5.hpp"
#else
#  include <core/region.h>
#endif

#include <QEasingCurve>
#include <QLoggingCategory>
#include <QVariant>
#include <QtNumeric>

#include <chrono>
#include <optional>

Q_LOGGING_CATEGORY(BBDX_WINDOW, "kwin_effect_better_blur_dx.window", QtInfoMsg)

BBDX::Window::Window(BBDX::WindowManager *wm, KWin::EffectWindow *w) {
    m_windowManager = wm;
    m_effectwindow = w;
    reconfigure();
    refreshMaximizedState();
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &BBDX::Window::slotMinimizedChanged);
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, &BBDX::Window::slotWindowFrameGeometryChanged);
    connect(w, &KWin::EffectWindow::windowStartUserMovedResized, this, &BBDX::Window::slotWindowStartUserMovedResized);
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, &BBDX::Window::slotWindowFinishUserMovedResized);
}

void BBDX::Window::slotMinimizedChanged() {
    if (m_maximizedState == MaximizedState::Complete
        && effectwindow()->isMinimized()) {
        m_restoresMaximized = true;
    }
}

void BBDX::Window::slotWindowStartUserMovedResized() {
    if (m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::ForcedContent)) {
        // Don't allow blurring while transformed during move/resize
        // to avoid dragging an off-looking rectangular blur region
        // behind windows affected by Wobbly Windows.
        m_shouldBlurWhileTransformed = true;
        m_blurWhileTransformedTransitionState = TransformState::Started;
        m_blurWhileTransformedTransitionStart = std::chrono::steady_clock::now();
    } else {
        m_shouldBlurWhileTransformed = false;
        m_blurWhileTransformedTransitionState = TransformState::None;
    }
}

void BBDX::Window::slotWindowFinishUserMovedResized() {
    if (m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::ForcedContent)) {
        // After move/resize force blurring while transformed.
        // While still suboptimal (the Wobbly Windows effect doesn't end
        // after finishing move/resize) this at least assures blur
        // is always set afterwards.
        m_shouldBlurWhileTransformed = true;
        m_blurWhileTransformedTransitionState = TransformState::Ended;
        m_blurWhileTransformedTransitionStart = std::chrono::steady_clock::now();
    } else {
        m_shouldBlurWhileTransformed = false;
        m_blurWhileTransformedTransitionState = TransformState::None;
    }
}

void BBDX::Window::slotWindowFrameGeometryChanged() {
    updateForceBlurRegion();
    refreshMaximizedState();

    // Not sure if this is the best place to unset
    // this but seems to work fine for now
    m_restoresMaximized = false;
}

void BBDX::Window::setIsTransformed(bool toggle) {
    if (m_isTransformed == toggle)
        return;

    m_isTransformed = toggle;

    // Unset m_shouldBlurWhileTransformed flag
    // on the first non-transformed paint.
    // Needed e.g. for Magic Lamp effect to not draw blur.
    if (!toggle) {
        m_shouldBlurWhileTransformed = false;
    }
}

void BBDX::Window::setMaximizedState(MaximizedState state) {
    m_maximizedState = state;
}

bool BBDX::Window::shouldBlurWhileTransformed() const {
    // While minimized there's no reason to blur
    if (effectwindow()->isMinimized()) {
        return false;
    }

    // While completely maximized we can always blur.
    // Avoids weirdness while (de-)maximzing by dragging the
    // titlebar with Wobbly Windows enabled.
    if (m_maximizedState == MaximizedState::Complete && !m_restoresMaximized) {
        return true;
    }

    return m_shouldBlurWhileTransformed;
}

void BBDX::Window::refreshMaximizedState() {
    m_windowManager->refreshMaximizedState(effectwindow());
}

void BBDX::Window::updateForceBlurRegion() {
    if (!m_shouldForceBlur) {
        if (m_forceBlurContent.has_value() || m_forceBlurFrame.has_value()) {
            m_forceBlurContent.reset();
            m_forceBlurFrame.reset();
            triggerBlurRegionUpdate();
        }
        return;
    }

    std::optional<KWin::Region> content{};
    std::optional<KWin::Region> frame{};

    // On X11, EffectWindow::contentsRect() includes GTK's client-side shadows, while on Wayland, it doesn't.
    // The content region is translated by EffectWindow::contentsRect() in BlurEffect::blurRegion, causing the
    // blur region to be off on X11. The frame region is not translated, so it is used instead.
    const auto isX11WithCSD = effectwindow()->isX11Client() &&
                              !effectwindow()->hasDecoration() &&
                              effectwindow()->frameGeometry() != effectwindow()->bufferGeometry();
    if (!isX11WithCSD) {
        // empty QRegion -> full window
        content = KWin::Region();

        // only decorations in this case
        if (m_windowManager->blurDecorations() && m_effectwindow->decoration()) {
            frame = KWin::Region(KWin::Rect(effectwindow()->decoration()->rect().toAlignedRect())) - effectwindow()->contentsRect().toRect();
        }
    } else {
        // frame is full window
        frame = KWin::Region(KWin::Rect(m_effectwindow->frameGeometry().translated(-m_effectwindow->x(), -m_effectwindow->y()).toRect()));
    }

    // unchanged
    if (content == m_forceBlurContent && frame == m_forceBlurFrame) {
        return;
    }

    m_forceBlurContent = std::move(content);
    m_forceBlurFrame = std::move(frame);
    triggerBlurRegionUpdate();
}

void BBDX::Window::triggerBlurRegionUpdate() {
    m_windowManager->triggerBlurRegionUpdate(m_effectwindow);
}

void BBDX::Window::reconfigure() {
    if (m_windowManager->shouldForceBlur(m_effectwindow)) {
        m_shouldForceBlur = true;
    } else {
        m_shouldForceBlur = false;
    }

    m_userBorderRadius = m_windowManager->userBorderRadius();

    updateForceBlurRegion();
}

void BBDX::Window::getFinalBlurRegion(std::optional<KWin::Region> &content, std::optional<KWin::Region> &frame) {
    unsigned int oldBlurOriginMask = m_blurOriginMask;

    // If we already have a blur region at this point
    // the window requested it.
    // This tracker allows us to later decide if we want
    // to trust the window or use user parameters
    // e.g. for corner radius.
    if (content.has_value()) {
        m_blurOriginMask |= static_cast<unsigned int>(BlurOrigin::RequestedContent);
        m_blurOriginMask &= ~static_cast<unsigned int>(BlurOrigin::ForcedContent);
    } else {
        m_blurOriginMask &= ~static_cast<unsigned int>(BlurOrigin::RequestedContent);
    }
    if (frame.has_value()) {
        m_blurOriginMask |= static_cast<unsigned int>(BlurOrigin::RequestedFrame);
        m_blurOriginMask &= ~static_cast<unsigned int>(BlurOrigin::ForcedFrame);
    } else {
        m_blurOriginMask &= ~static_cast<unsigned int>(BlurOrigin::RequestedFrame);
    }

    // Normally we'd assume windows that set their own blur region
    // know what they're doing.
    // However these windows probably don't expect users to decrease
    // the window opacity via KWin rules in which case we'll allow
    // overriding the blur area.
    // (w->opacity() here is the *entire* windows opacity incl. decorations i.e. what KWin rules change.
    // Most windows will provide opacity via WindowPaintData)
    if (m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::RequestedContent)
        && qFuzzyCompare(m_effectwindow->opacity(), 1.0)) {
        if (m_blurOriginMask != oldBlurOriginMask) {
            qCInfo(BBDX_WINDOW) << BBDX::LOG_PREFIX << "Blur origin changed:" << *this
                                                    << "(was:" << Window::blurOriginToString(oldBlurOriginMask) << ")";
        }
        return;
    }

    // Apply potentially set forceblur regions
    // if (and only if) set in updateForceBlurRegion().
    // We can't set these unconditionally (especially frame) because
    // custom decorations might have set their own blur region.
    if (m_forceBlurContent.has_value()) {
        content = m_forceBlurContent;
        m_blurOriginMask |= static_cast<unsigned int>(BlurOrigin::ForcedContent);
        m_blurOriginMask &= ~static_cast<unsigned int>(BlurOrigin::RequestedContent);
    } else {
        m_blurOriginMask &= ~static_cast<unsigned int>(BlurOrigin::ForcedContent);
    }

    // Only override frame if it doesn't already specify a blur region.
    // The provided one is likely more accurate (e.g. already has its corners rounded
    // or an outline that shouldn't be blured)
    // (XXX: Shouldn't interfere with the "isX11WithCSD" hack
    // because CSD windows should never have a frame already set)
    if (m_forceBlurFrame.has_value() && !frame.has_value()) {
        frame = m_forceBlurFrame;
        m_blurOriginMask |= static_cast<unsigned int>(BlurOrigin::ForcedFrame);
        m_blurOriginMask &= ~static_cast<unsigned int>(BlurOrigin::RequestedFrame);
    } else {
        m_blurOriginMask &= ~static_cast<unsigned int>(BlurOrigin::ForcedFrame);
    }

    if (m_blurOriginMask != oldBlurOriginMask) {
        qCInfo(BBDX_WINDOW) << BBDX::LOG_PREFIX << "Blur origin changed:" << *this
                                                << "(was:" << Window::blurOriginToString(oldBlurOriginMask) << ")";
    }

    // this isn't fatal but we only expect one to be set, so log if that's not the case
    if ((m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::RequestedContent))
            && (m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::ForcedContent))) {
        qCWarning(BBDX_WINDOW) << BBDX::LOG_PREFIX
                               << "BlurOrigin::RequestedContent and BlurOrigin::ForcedContent"
                               << "both set on window"
                               << effectwindow()->window()->resourceClass();
    }

    if ((m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::RequestedFrame))
            && (m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::ForcedFrame))) {
        qCWarning(BBDX_WINDOW) << BBDX::LOG_PREFIX
                               << "BlurOrigin::RequestedFrame and BlurOrigin::ForcedFrame"
                               << "both set on window"
                               << effectwindow()->window()->resourceClass();
    }
}

KWin::BorderRadius BBDX::Window::getEffectiveBorderRadius() {
    // always respect window provided radius
    // (although this seems to only ever be set by Breeze's
    // "Round bottom corners of windows with no borders"
    // currently)
    const KWin::BorderRadius windowCornerRadius = m_effectwindow->window()->borderRadius();

    // Plasma surfaces set their blur region
    // in a way that *should* not bleed
    if (isPlasmaSurface()) {
        return windowCornerRadius;
    }

    // Breeze has a "Round bottom corners of windows with no borders"
    // option which sets radius for the *bottom corners only* (because
    // it assumes no blur behind the top corners).
    // We provide a "Blur decorations as well option" which breaks said
    // assumption. Assuming all corners are rounded equally we'll just
    // copy the bottom radius to their respective top corners.
    if (!windowCornerRadius.isNull()) {
        // No adjustments relevant for CSD windows
        if (!effectwindow()->hasDecoration())
            return windowCornerRadius;

        // If not force-blurring decorations we don't need
        // any adjustments
        if (!(m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::ForcedFrame)))
            return windowCornerRadius;

        // if top radius is set explicitly by decoration keep it
        const qreal topLeft = windowCornerRadius.topLeft() > 0.0 ?
            windowCornerRadius.topLeft() : windowCornerRadius.bottomLeft();
        const qreal topRight = windowCornerRadius.topRight() > 0.0 ?
            windowCornerRadius.topRight() : windowCornerRadius.bottomRight();

        return KWin::BorderRadius(topLeft,
                                  topRight,
                                  windowCornerRadius.bottomLeft(),
                                  windowCornerRadius.bottomRight());
    }

    // Fullscreen/completely maximized windows don't need radius.
    if (m_effectwindow->isFullScreen() || m_maximizedState == MaximizedState::Complete) {
        return KWin::BorderRadius();
    }

    // fallback to configured radius
    if (m_userBorderRadius > 0.0) {
        // If decoration is blurred or window has CSD we need to round all corners
        // because blur might bleed from all corners.
        // Else we assume the decoration isn't blurred at all i.e. the top corners
        // are already correct and we only need to round the bottom ones.
        if (m_blurOriginMask & (static_cast<unsigned int>(BlurOrigin::RequestedFrame) | static_cast<unsigned int>(BlurOrigin::ForcedFrame))) {
            return KWin::BorderRadius(m_userBorderRadius);
        } else if (!effectwindow()->hasDecoration()) {
            return KWin::BorderRadius(m_userBorderRadius);
        } else {
            return KWin::BorderRadius(0.0, 0.0, m_userBorderRadius, m_userBorderRadius);
        }
    } else {
        return KWin::BorderRadius();
    }
}

qreal BBDX::Window::getEffectiveBlurOpacity(KWin::WindowPaintData &data) {
    // Plasma surfaces expect their opacity to affect
    // the blur e.g. to hide the blurred surface alongside
    // themselves.
    // Force blurred surfaces don't want/need this
    if (isPlasmaSurface()) {
        return effectwindow()->opacity() * data.opacity();
    }

    // ease into and out of the phase without blur
    // if moving/resizing with Wobbly Windows
    // TODO: maybe move partly this to a separate function
    //       - feels kind of out-of place here
    if (m_isTransformed && m_blurWhileTransformedTransitionState != TransformState::None) [[unlikely]] {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_blurWhileTransformedTransitionStart).count();

        // animation takes 250ms max for now
        const qreal progress = elapsed / 250.0;

        // we need to queue a full repaint here to 
        // avoid flickering due to blur-region-clipping
        effectwindow()->addRepaintFull();

        switch (m_blurWhileTransformedTransitionState) {
            case TransformState::Started:
                // De-maximizing a window by dragging the titlebar
                // while wobbly windows is active behaves weird:
                // - drag (before de-maximize) already marked "transformed" after mouse moved a bit
                // - on actual de-maximize blur briefly reappears (not marked "transformed"?)
                // - then it's marked "transformed" again
                // So while we're maximized stay fully blurred.
                if (m_maximizedState == MaximizedState::Complete) {
                    m_blurWhileTransformedTransitionStart = std::chrono::steady_clock::now();
                    return data.opacity();
                }

                if (progress >= 1.0) {
                    // fade out done
                    // We can stop blurring now until
                    // slotWindowFinishUserMovedResized gets called.
                    m_shouldBlurWhileTransformed = false;
                    return 0.0;
                } else {
                    // fade out in progress
                    const QEasingCurve curve{QEasingCurve::OutCubic};
                    return data.opacity() * (1.0 - curve.valueForProgress(progress));
                }
            case TransformState::Ended:
                // Needed to avoid a flicker in case de-maximizing by dragging
                // the titlebar is cancelled (drag stopped before de-maximize)
                // which still triggers slotWindowFinishUserMovedResized
                // TODO: this should only be the case if MaximizedState stays Complete
                //       throughout the entire UserMovedResized i.e. we need to track that
                if (m_maximizedState == MaximizedState::Complete) {
                    m_blurWhileTransformedTransitionState = TransformState::None;
                    return data.opacity();
                }

                if (progress >= 1.0) {
                    // fade in done
                    m_blurWhileTransformedTransitionState = TransformState::None;
                    return data.opacity();
                } else {
                    // fade in in progress
                    const QEasingCurve curve{QEasingCurve::InCubic};
                    return data.opacity() * curve.valueForProgress(progress);
                }
            default:
                break;
        }
    }

    return data.opacity();
}

bool BBDX::Window::isPlasmaSurface() const {
    // Plasma surfaces must specify their own blur
    if (!(m_blurOriginMask & static_cast<unsigned int>(BlurOrigin::RequestedContent)))
        return false;

    // Plasma surfaces (afaik) never have decorations
    if (effectwindow()->hasDecoration())
        return false;

    // These window classes are known to be Plasma surfaces
    const auto resourceClass = effectwindow()->window()->resourceClass();
    if (resourceClass == QStringLiteral("org.kde.plasmashell")
        || resourceClass == QStringLiteral("plasmashell")
        || resourceClass == QStringLiteral("org.kde.krunner")
        || resourceClass == QStringLiteral("krunner"))
        return true;

    // If a window is special it's very likely a Plasma surface
    if (effectwindow()->isSpecialWindow())
        return true;

    // Some popups like the Meta+Ctrl+ESC killer have an empty window class,
    // but are still considered KWin::WindowType::Normal
    // Let's just assume an empty class means they're likely Plasma surfaces as well
    if (resourceClass == QStringLiteral(""))
        return true;

    // Likely not a Plasma surface then
    return false;
}

QString BBDX::Window::blurOriginToString(unsigned int mask) {
    QString s{};
    if (mask & static_cast<unsigned int>(BlurOrigin::RequestedContent))
        s.append("RequestedContent,");
    if (mask & static_cast<unsigned int>(BlurOrigin::RequestedFrame))
        s.append("RequestedFrame,");
    if (mask & static_cast<unsigned int>(BlurOrigin::ForcedContent))
        s.append("ForcedContent,");
    if (mask & static_cast<unsigned int>(BlurOrigin::ForcedFrame))
        s.append("ForcedFrame,");

    if (s.isEmpty()) {
        s = "None";
    } else {
        s.removeLast();
    }

    return s;
}

namespace BBDX {
QDebug operator<<(QDebug &debug, const BBDX::Window &window) {
    debug << "windowClass:" << window.effectwindow()->windowClass();
    debug << "windowType:" << window.effectwindow()->windowType();
    debug << "blurOrigin:" << Window::blurOriginToString(window.m_blurOriginMask);
    return debug;
}
} // namespace BBDX
