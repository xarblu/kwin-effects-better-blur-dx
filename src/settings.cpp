#include "settings.h"
#include "blurconfig.h"

namespace KWin
{

void BlurSettings::read()
{
    BlurConfig::self()->read();

    general.blurStrength = BlurConfig::blurStrength() - 1;
    general.noiseStrength = BlurConfig::noiseStrength();
    general.brightness = BlurConfig::brightness() / 100.0;
    general.saturation = BlurConfig::saturation() / 100.0;
    general.contrast = BlurConfig::contrast() / 100.0;
    general.forceContrastParams = BlurConfig::forceContrastParams();
    general.cornerRadius = BlurConfig::cornerRadius();

    forceBlur.blurDecorations = BlurConfig::blurDecorations();
    forceBlur.blurMenus = BlurConfig::blurMenus();
    forceBlur.blurDocks = BlurConfig::blurDocks();
}

}
