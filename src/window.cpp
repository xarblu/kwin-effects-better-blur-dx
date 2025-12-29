#include "window.hpp"
#include "window_manager.hpp"

#include <effect/effectwindow.h>
#include <KDecoration3/Decoration>

#include <optional>

BBDX::Window::Window(KWin::EffectWindow *w) {
    m_effectwindow = w;
    reconfigure();
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, &BBDX::Window::slotWindowFrameGeometryChanged);
}

void BBDX::Window::slotWindowFrameGeometryChanged() {
    updateForceBlurRegion();
}

void BBDX::Window::updateForceBlurRegion() {
    auto windowManager = BBDX::WindowManager::instance();
    if (!windowManager)
        return;

    if (!m_forceBlurred) {
        m_forceBlurContent.reset();
        m_forceBlurFrame.reset();
        triggerBlurRegionUpdate();
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
        if (windowManager->blurDecorations() && m_effectwindow->decoration()) {
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
    auto windowManager = BBDX::WindowManager::instance();
    if (!windowManager)
        return;

    windowManager->triggerBlurRegionUpdate(m_effectwindow);
}

void BBDX::Window::reconfigure() {
    auto windowManager = BBDX::WindowManager::instance();
    if (!windowManager)
        return;

    if (windowManager->shouldForceBlur(m_effectwindow)) {
        m_forceBlurred = true;
    } else {
        m_forceBlurred = false;
    }

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
