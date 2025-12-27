#include "window.hpp"
#include "window_manager.hpp"

#include <effect/effectwindow.h>
#include <KDecoration3/Decoration>

BBDX::Window::Window(KWin::EffectWindow *w) {
    m_effectwindow = w;
    reconfigure();
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, &BBDX::Window::slotWindowFrameGeometryChanged);
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, &BBDX::Window::slotWindowFinishUserMovedResized);
}

void BBDX::Window::slotWindowFrameGeometryChanged() {
    updateForceBlurRegion();
}

void BBDX::Window::slotWindowFinishUserMovedResized() {
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

    // On X11, EffectWindow::contentsRect() includes GTK's client-side shadows, while on Wayland, it doesn't.
    // The content region is translated by EffectWindow::contentsRect() in BlurEffect::blurRegion, causing the
    // blur region to be off on X11. The frame region is not translated, so it is used instead.
    const auto isX11WithCSD = m_effectwindow->isX11Client() &&
                              m_effectwindow->frameGeometry() != m_effectwindow->bufferGeometry();
    if (!isX11WithCSD) {
        // empty QRegion -> full window
        m_forceBlurContent = QRegion();

        // only decorations in this case
        if (windowManager->blurDecorations() && m_effectwindow->decoration()) {
            m_forceBlurFrame = QRegion(m_effectwindow->decoration()->rect().toAlignedRect()) - m_effectwindow->contentsRect().toRect();
        }
    } else {
        // frame is full window
        m_forceBlurFrame = m_effectwindow->frameGeometry().translated(-m_effectwindow->x(), -m_effectwindow->y()).toRect();
    }

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
