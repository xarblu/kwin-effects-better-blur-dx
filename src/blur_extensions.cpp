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

void BlurEffect::updateForceBlurRegion(const EffectWindow *w, std::optional<QRegion> &content, std::optional<QRegion> &frame, BlurType &type)
{
    // we'll assume windows that set their own blur region
    // know what they're doing
    if (content.has_value()) return;

    // don't touch KWin internal windows
    // this includes the snapping assistant zones
    // and they don't handle blur well at all
    if (w->internalWindow()) return;

    if (!shouldForceBlur(w)) return;

    // On X11, EffectWindow::contentsRect() includes GTK's client-side shadows, while on Wayland, it doesn't.
    // The content region is translated by EffectWindow::contentsRect() in BlurEffect::blurRegion, causing the
    // blur region to be off on X11. The frame region is not translated, so it is used instead.
    const auto isX11WithCSD = w->isX11Client() && w->frameGeometry() != w->bufferGeometry();
    if (!isX11WithCSD) {
        content = w->contentsRect().translated(-w->contentsRect().topLeft()).toRect();
    }
    if (isX11WithCSD || (m_settings.forceBlur.blurDecorations && w->decoration())) {
        frame = w->frameGeometry().translated(-w->x(), -w->y()).toRect();
    }

    type = BlurType::Forced;
}

BorderRadius BlurEffect::getWindowBorderRadius(EffectWindow *w)
{
    // init of BlurEffect::blur() should w is in map
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

}
