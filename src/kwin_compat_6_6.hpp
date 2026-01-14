#pragma once

#include "kwin_version.hpp"

// Some common compatibility things

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
class QRect;
class QRectF;
class QRegion;
namespace KWin {
    using Rect = QRect;
    using RectF = QRectF;
    using Region = QRegion;
}
#endif

