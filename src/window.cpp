#include "window.hpp"
#include "window_manager.hpp"

#include <KDecoration3/Decoration>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <scene/borderradius.h>
#include <window.h>

#include <QVariant>

#include <optional>

BBDX::Window::Window(BBDX::WindowManager *wm, KWin::EffectWindow *w) {
    m_windowManager = wm;
    m_effectwindow = w;
    reconfigure();
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, &BBDX::Window::slotWindowFrameGeometryChanged);
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, this, &BBDX::Window::slotWindowMaximizedStateChanged);
    connect(w, &KWin::EffectWindow::windowStartUserMovedResized, this, &BBDX::Window::slotWindowStartUserMovedResized);
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, &BBDX::Window::slotWindowFinishUserMovedResized);
}

void BBDX::Window::slotWindowStartUserMovedResized() {
    effectwindow()->setData(KWin::WindowForceBlurRole, QVariant());
}

void BBDX::Window::slotWindowFinishUserMovedResized() {
    effectwindow()->setData(KWin::WindowForceBlurRole, QVariant(true));
}

void BBDX::Window::slotWindowFrameGeometryChanged() {
    updateForceBlurRegion();
}

void BBDX::Window::slotWindowMaximizedStateChanged(bool horizontal, bool vertical) {
    if (!horizontal && !vertical) {
        m_maximizedState = MaximizedState::Restored;
    } else if (horizontal && !vertical) {
        m_maximizedState = MaximizedState::Horizontal;
    } else if (!horizontal && vertical) {
        m_maximizedState = MaximizedState::Vertical;
    } else if (horizontal && vertical) {
        m_maximizedState = MaximizedState::Complete;
    } else {
        m_maximizedState = MaximizedState::Unknown;
    }
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

    std::optional<QRegion> content{};
    std::optional<QRegion> frame{};

    // On X11, EffectWindow::contentsRect() includes GTK's client-side shadows, while on Wayland, it doesn't.
    // The content region is translated by EffectWindow::contentsRect() in BlurEffect::blurRegion, causing the
    // blur region to be off on X11. The frame region is not translated, so it is used instead.
    const auto isX11WithCSD = m_effectwindow->isX11Client() &&
                              m_effectwindow->frameGeometry() != m_effectwindow->bufferGeometry();
    if (!isX11WithCSD) {
        // empty QRegion -> full window
        content = QRegion();

        // only decorations in this case
        if (m_windowManager->blurDecorations() && m_effectwindow->decoration()) {
            frame = QRegion(m_effectwindow->decoration()->rect().toAlignedRect()) - m_effectwindow->contentsRect().toRect();
        }
    } else {
        // frame is full window
        frame = m_effectwindow->frameGeometry().translated(-m_effectwindow->x(), -m_effectwindow->y()).toRect();
    }

    bool changed{false};
    if (content != m_forceBlurContent || frame != m_forceBlurFrame) {
        changed = true;
    }

    m_forceBlurContent = std::move(content);
    m_forceBlurFrame = std::move(frame);

    if (changed)
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

void BBDX::Window::getFinalBlurRegion(std::optional<QRegion> &content, std::optional<QRegion> &frame) {
    // If we already have a blur region at this point
    // the window requested it.
    // This tracker allows us to later decide if we want
    // to trust the window or use user parameters
    // e.g. for corner radius.
    if (content.has_value() || frame.has_value()) {
        m_requestedBlur = true;
    } else {
        m_requestedBlur = false;
    }

    // Normally we'd assume windows that set their own blur region
    // know what they're doing.
    // However these windows probably don't expect users to decrease
    // the window opacity via KWin rules in which case we'll allow
    // overriding the blur area.
    // (w->opacity() here is the *entire* windows opacity incl. decorations i.e. what KWin rules change.
    // Most windows will provide opacity via WindowPaintData)
    if (content.has_value() && m_effectwindow->opacity() >= 1.0) return;

    // matched by user config
    content = m_forceBlurContent;
    frame = m_forceBlurFrame;
    m_requestedBlur = false;
}

KWin::BorderRadius BBDX::Window::getEffectiveBorderRadius() {
    // always respect window provided radius
    // (although this seems to only ever be set by Breeze's
    // "Round bottom corners of windows with no borders"
    // currently)
    const KWin::BorderRadius windowCornerRadius = m_effectwindow->window()->borderRadius();

    // Breeze has a "Round bottom corners of windows with no borders"
    // option which sets radius for the *bottom corners only* (because
    // it assumes no blur behind the top corners).
    // We provide a "Blur decorations as well option" which breaks said
    // assumption. Assuming all corners are rounded equally we'll just
    // copy the bottom radius to their respective top corners.
    if (!windowCornerRadius.isNull()) {
        // if not blurring decorations we don't need
        // any adjustments
        if (!m_windowManager->blurDecorations())
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

    // assume the window knows what it's doing
    // when it requested the blur
    if (m_requestedBlur) {
        return KWin::BorderRadius();
    }

    // Maximized/fullscreen windows don't need radius.
    // They shouldn't have rounded corners.
    // TODO: Apparently this doesn't cover tiles
    //       but there is no easy way to detect those.
    //       They look maximized, behave maximized but
    //       apparently aren't maximized.
    if (m_effectwindow->isFullScreen()
        || m_maximizedState == MaximizedState::Horizontal
        || m_maximizedState == MaximizedState::Vertical
        || m_maximizedState == MaximizedState::Complete) {
        return KWin::BorderRadius();
    }

    // fallback to configured radius
    if (m_userBorderRadius > 0.0) {
        return KWin::BorderRadius(m_userBorderRadius);
    } else {
        return KWin::BorderRadius();
    }
}
