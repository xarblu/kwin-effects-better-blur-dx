#pragma once

#include <core/output.h>

// Compatibility bits for "upgrading"
// KWin 6.5 API to 6.6
// (and KWin-X11 to KWin-Wayland... *sigh*)

class QRect;
class QRectF;
class QRegion;

namespace KWin {
    using Rect = QRect;
    using RectF = QRectF;
    using Region = QRegion;
    using LogicalOutput = KWin::Output;
}
