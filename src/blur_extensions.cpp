/*
 * File containing functions only present in
 * Better Blur DX
 */


#include "blur.h"
#include "utils.h"
#include "window.hpp"

#include <effect/effectwindow.h>
#include <scene/borderradius.h>

#include <KDecoration3/Decoration>

#include <QRegion>

namespace KWin
{

void BlurEffect::slotWindowMaximizedStateChanged(EffectWindow *w, bool horizontal, bool vertical) {
    auto it = m_windows.find(w);
    if (it == m_windows.end()) {
        return;
    }

    BlurEffectData &data = it->second;

    if (!horizontal && !vertical) {
        data.maximizedState = MaximizedState::Restored;
    } else if (horizontal && !vertical) {
        data.maximizedState = MaximizedState::Horizontal;
    } else if (!horizontal && vertical) {
        data.maximizedState = MaximizedState::Vertical;
    } else if (horizontal && vertical) {
        data.maximizedState = MaximizedState::Complete;
    } else {
        data.maximizedState = MaximizedState::Unknown;
    }
}

void BlurEffect::updateForceBlurRegion(const EffectWindow *w, std::optional<QRegion> &content, std::optional<QRegion> &frame, BlurType &type)
{
    // If we already have a blur region at this point
    // the window requested it.
    // This tracker allows us to later decide if we want
    // to trust the window or use user parameters
    // e.g. for corner radius.
    if (content.has_value() || frame.has_value()) {
        type = BlurType::Requested;
    }

    // Normally we'd assume windows that set their own blur region
    // know what they're doing.
    // However these windows probably don't expect users to decrease
    // the window opacity via KWin rules in which case we'll allow
    // overriding the blur area.
    // (w->opacity() here is the *entire* windows opacity incl. decorations i.e. what KWin rules change.
    // Most windows will provide opacity via WindowPaintData)
    if (content.has_value() && w->opacity() >= 1.0) return;

    // matched by user config
    if (const BBDX::Window* window = m_windowManager.findWindow(w)) {
        content = window->forceBlurContent();
        frame = window->forceBlurFrame();
        type = BlurType::Forced;
    }

}

BorderRadius BlurEffect::getWindowBorderRadius(EffectWindow *w)
{
    // init of BlurEffect::blur() should assure w is in map
    BlurEffectData &data = m_windows[w];

    // always respect window provided radius
    const BorderRadius windowCornerRadius = w->window()->borderRadius();
    if (!windowCornerRadius.isNull()) {
        return windowCornerRadius;
    }

    // assume the window knows what it's doing
    // when it requested the blur
    if (data.type == BlurType::Requested) {
        return BorderRadius();
    }

    // Maximized/fullscreen windows don't need radius.
    // They shouldn't have rounded corners.
    // TODO: Apparently this doesn't cover tiles
    //       but there is no easy way to detect those.
    //       They look maximized, behave maximized but
    //       apparently aren't maximized.
    if (w->isFullScreen()
        || data.maximizedState == MaximizedState::Horizontal
        || data.maximizedState == MaximizedState::Vertical
        || data.maximizedState == MaximizedState::Complete) {
        return BorderRadius();
    }

    // fallback to configured radius
    if (qreal radius = m_settings.general.cornerRadius; radius > 0.0) {
        return BorderRadius(radius);
    } else {
        return BorderRadius();
    }
}

qreal BlurEffect::getContrastParam(std::optional<qreal> requested_value, qreal config_value) const
{
    if (m_settings.general.forceContrastParams) {
        return config_value;
    }

    return requested_value.value_or(config_value);
}

qreal BlurEffect::getOpacity(const EffectWindow *w, WindowPaintData &data, BlurEffectData &blurInfo) const
{
    // Plasma surfaces expect their opacity to affect
    // the blur e.g. to hide the blurred surface alongside
    // themselves.
    // Force blurred surfaces don't want/need this
    if (blurInfo.type == BlurType::Requested) {
        return w->opacity() * data.opacity();
    } else {
        return data.opacity();
    }
}

}
