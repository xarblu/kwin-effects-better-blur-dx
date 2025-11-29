/*
 * File containing functions only present in
 * Better Blur DX
 */


#include "blur.h"
#include "utils.h"

#include <effect/effectwindow.h>
#include <scene/borderradius.h>

namespace KWin
{

void BlurEffect::slotWindowClosed(EffectWindow *w)
{
    /*
     * Some windows (e.g. the foot terminal) like to
     * close by simply deleting themselves.
     * This causes e.g. the "scale" window close effect
     * to blur and apply contrast to a fully transparent area.
     *
     * With modified contrast/saturation this at best looks bad
     * and at worst causes a blinding flash with bright background surfaces.
     * Clamp them to mostly work around the issue.
     */
    if (w && w->isDeleted()) {
        BlurEffectData &data = m_windows[w];

        const qreal brightness = data.brightness.value_or(m_settings.general.brightness);
        data.brightness = std::min(brightness, 1.0);

        const qreal saturation = data.saturation.value_or(m_settings.general.saturation);
        data.saturation = std::min(saturation, 1.0);

        const qreal contrast = data.contrast.value_or(m_settings.general.contrast);
        data.contrast = std::min(contrast, 1.0);
    }
}

bool BlurEffect::shouldForceBlur(const EffectWindow *w) const
{
    const auto windowClass = w->window()->resourceClass();
    const auto layer = w->window()->layer();
    if (w->isDesktop()
        || (!m_settings.forceBlur.blurDocks && w->isDock())
        || (!m_settings.forceBlur.blurMenus && isMenu(w))
        || windowClass == "xwaylandvideobridge"
        || ((windowClass == "spectacle" || windowClass == "org.kde.spectacle")
            && (layer == Layer::OverlayLayer || layer == Layer::ActiveLayer))) {
        return false;
    }

    bool matches = m_settings.forceBlur.windowClasses.contains(w->window()->resourceName())
        || m_settings.forceBlur.windowClasses.contains(w->window()->resourceClass());
    return (matches && m_settings.forceBlur.windowClassMatchingMode == WindowClassMatchingMode::Whitelist)
        || (!matches && m_settings.forceBlur.windowClassMatchingMode == WindowClassMatchingMode::Blacklist);
}

BorderRadius BlurEffect::getWindowBorderRadius(const EffectWindow *w) const
{
    const BorderRadius windowCornerRadius = w->window()->borderRadius();
    if (!windowCornerRadius.isNull()) {
        return windowCornerRadius;
    }

    if (qreal radius = m_settings.general.cornerRadius; radius > 0.0) {
        return BorderRadius(radius);
    } else {
        return BorderRadius();
    }
}

}
