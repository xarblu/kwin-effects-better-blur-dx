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
#include <QEasingCurve>

#include <chrono>

namespace KWin
{

void BlurEffect::slotWindowWantsBlurRegionUpdate(EffectWindow *w) {
    updateBlurRegion(w);
}

qreal BlurEffect::getContrastParam(std::optional<qreal> requested_value, qreal config_value) const
{
    if (m_settings.general.forceContrastParams) {
        return config_value;
    }

    return requested_value.value_or(config_value);
}

}
