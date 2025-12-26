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
    qreal brightness;
    qreal saturation;
    qreal contrast;
    bool forceContrastParams;
    qreal cornerRadius;
};

struct ForceBlurSettings
{
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
