/*
 * File containing functions only present in
 * Better Blur DX
 */


#include "blur.h"
#include "window.hpp"

#include <effect/effectwindow.h>
#include <scene/borderradius.h>

#include <KDecoration3/Decoration>

#include <QRegion>
#include <QEasingCurve>


namespace KWin
{

void BlurEffect::slotWindowWantsBlurRegionUpdate(EffectWindow *w) {
    updateBlurRegion(w);
}

}
