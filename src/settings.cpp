#include "settings.h"
#include "blurconfig.h"

namespace KWin
{

void BlurSettings::read()
{
    BlurConfig::self()->read();

    general.blurStrength = BlurConfig::blurStrength() - 1;
    general.noiseStrength = BlurConfig::noiseStrength();
    general.windowOpacityAffectsBlur = BlurConfig::transparentBlur();
    general.brightness = BlurConfig::brightness() / 100.0;
    general.saturation = BlurConfig::saturation() / 100.0;
    general.contrast = BlurConfig::contrast() / 100.0;
    general.forceContrastParams = BlurConfig::forceContrastParams();
    general.cornerRadius = BlurConfig::cornerRadius();

    forceBlur.windowClasses.clear();
    const auto blank = QStringLiteral("blank");
    for (const auto &line : BlurConfig::windowClasses().split("\n", Qt::SkipEmptyParts)) {
        QString unescaped = "";
        bool consumed = false;
        for (qsizetype i = 0; i < line.size(); i++) {
            const auto character = line[i];
            if (character == QChar('$') && !consumed) {
                consumed = true;
                continue;
            }
            if (consumed) {
                const qsizetype skips = blank.size();
                if (line.mid(i, skips) == blank) {
                    consumed = false;
                    i += skips - 1;
                    continue;
                }
            }
            consumed = false;
            unescaped += character;
        }
        if (consumed) {
            unescaped += QChar('$');
        }
        forceBlur.windowClasses << unescaped;
    }
    forceBlur.windowClassMatchingMode = BlurConfig::blurMatching() ? WindowClassMatchingMode::Whitelist : WindowClassMatchingMode::Blacklist;
    forceBlur.blurDecorations = BlurConfig::blurDecorations();
    forceBlur.blurMenus = BlurConfig::blurMenus();
    forceBlur.blurDocks = BlurConfig::blurDocks();
}

}
