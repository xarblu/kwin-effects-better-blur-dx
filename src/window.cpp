#include "window.hpp"
#include "window_manager.hpp"

#include <effect/effectwindow.h>

BBDX::Window::Window(KWin::EffectWindow *w) {
    m_effectwindow = w;
    updateForceBlurRegion();
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, &BBDX::Window::slotFrameGeometryChanged);
}

void BBDX::Window::slotFrameGeometryChanged() {
    updateForceBlurRegion();
}

void BBDX::Window::updateForceBlurRegion() {
    auto windowManager = BBDX::WindowManager::instance();
    if (!windowManager)
        return;

    
    
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
}
