#pragma once

#include <QImage>
#include <QStringList>

namespace KWin
{

enum class WindowClassMatchingMode
{
    Blacklist,
    Whitelist
};


struct GeneralSettings
{
    int blurStrength;
    int noiseStrength;
    bool windowOpacityAffectsBlur;
    qreal brightness;
    qreal saturation;
    qreal contrast;
    bool forceContrastParams;
    qreal cornerRadius;
};

struct ForceBlurSettings
{
    QStringList windowClasses;
    WindowClassMatchingMode windowClassMatchingMode;
    bool blurDecorations;
    bool blurMenus;
    bool blurDocks;
};

class BlurSettings
{
public:
    GeneralSettings general{};
    ForceBlurSettings forceBlur{};

    void read();
};

}
