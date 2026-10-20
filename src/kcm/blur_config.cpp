/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "blur_config.h"

//#include <config-kwin.h>

// KConfigSkeleton
#include "blurconfig.h"

#include <KPluginFactory>
#include "kwineffects_interface.h"
#include "settings.hpp"

#include <KLocalizedString>
#include <QFileDialog>
#include <QPushButton>

namespace BBDX {

K_PLUGIN_CLASS(BlurEffectConfig)

BlurEffectConfig::BlurEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    KLocalizedString::setApplicationDomain("kwin_better_blur_dx");
    ui.setupUi(widget());
    BlurConfig::instance("kwinrc");
    addConfig(BlurConfig::self(), widget());

    QFile about(":/effects/better_blur_dx/kcm/about.html");
    if (about.open(QIODevice::ReadOnly)) {
        const auto html = about.readAll()
            .replace("${version}", ABOUT_VERSION_STRING)
            .replace("${repo}", "https://github.com/xarblu/kwin-effects-better-blur-dx");
        ui.aboutText->setHtml(html);
    }

    setupContextualHelp();
    setupSpinboxSliderSync();
    setupConstraints();

    connect(ui.kcfg_RefractionMode, &QComboBox::currentIndexChanged, this, &BlurEffectConfig::slotRefractionModeChanged);
    slotRefractionModeChanged(ui.kcfg_RefractionMode->currentIndex());
}

BlurEffectConfig::~BlurEffectConfig() {}

void BlurEffectConfig::setContextualHelp(
    KContextualHelpButton *const contextualHelpButton,
    const QString &text,
    QWidget *const heightHintWidget
) {
    contextualHelpButton->setContextualHelpText(text);
    if (heightHintWidget) {
        const auto ownHeightHint = contextualHelpButton->sizeHint().height();
        const auto otherHeightHint = heightHintWidget->sizeHint().height();
        if (ownHeightHint >= otherHeightHint) {
            contextualHelpButton->setHeightHintWidget(heightHintWidget);
        }
    }
}

void BlurEffectConfig::setupContextualHelp() {
    setContextualHelp(
        ui.windowClassesContextualHelp,
        QStringLiteral("<p>Specify one window class pattern per line.</p>") +

        QStringLiteral("<p><strong>Exact match:</strong><br>") +
        QStringLiteral("By default window classes are matched exactly.<br>") +
        QStringLiteral("Example: <code>org.kde.dolphin</code> matches only Dolphin.</p>") +

        QStringLiteral("<p><strong>Regex match:</strong><br>") +
        QStringLiteral("If wrapped with <code>/</code> window classes are matched by Perl compatible regular expression.<br>") +
        QStringLiteral("Example: <code>/^org\\.kde\\..*/</code> matches all KDE applications.</p>") +

        QStringLiteral("<p><strong>Empty match:</strong><br>") +
        QStringLiteral("Use the special value <code>$blank</code> to match empty window classes.</p>"),
        ui.windowClassesBriefDescription
    );
}

void BlurEffectConfig::setupSpinboxSliderSync() {
    // Blur Strength
    ui.spinboxBlurStrength->setValue(ui.kcfg_BlurStrength->value());
    connect(ui.kcfg_BlurStrength, &QSlider::valueChanged, this, [this](int value) {
            if (ui.spinboxBlurStrength->value() != value) ui.spinboxBlurStrength->setValue(value); });
    connect(ui.spinboxBlurStrength, &QSpinBox::valueChanged, this, [this](int value) {
            if (ui.kcfg_BlurStrength->value() != value) ui.kcfg_BlurStrength->setValue(value); });

    // Noise Strength
    ui.spinboxNoiseStrength->setValue(ui.kcfg_NoiseStrength->value());
    connect(ui.kcfg_NoiseStrength, &QSlider::valueChanged, this, [this](int value) {
            if (ui.spinboxNoiseStrength->value() != value) ui.spinboxNoiseStrength->setValue(value); });
    connect(ui.spinboxNoiseStrength, &QSpinBox::valueChanged, this, [this](int value) {
            if (ui.kcfg_NoiseStrength->value() != value) ui.kcfg_NoiseStrength->setValue(value); });

    // Brightness
    ui.spinboxBrightness->setValue(ui.kcfg_Brightness->value());
    connect(ui.kcfg_Brightness, &QSlider::valueChanged, this, [this](int value) {
            if (ui.spinboxBrightness->value() != value) ui.spinboxBrightness->setValue(value); });
    connect(ui.spinboxBrightness, &QSpinBox::valueChanged, this, [this](int value) {
            if (ui.kcfg_Brightness->value() != value) ui.kcfg_Brightness->setValue(value); });

    // Saturation
    ui.spinboxSaturation->setValue(ui.kcfg_Saturation->value());
    connect(ui.kcfg_Saturation, &QSlider::valueChanged, this, [this](int value) {
            if (ui.spinboxSaturation->value() != value) ui.spinboxSaturation->setValue(value); });
    connect(ui.spinboxSaturation, &QSpinBox::valueChanged, this, [this](int value) {
            if (ui.kcfg_Saturation->value() != value) ui.kcfg_Saturation->setValue(value); });

    // Contrast
    ui.spinboxContrast->setValue(ui.kcfg_Contrast->value());
    connect(ui.kcfg_Contrast, &QSlider::valueChanged, this, [this](int value) {
            if (ui.spinboxContrast->value() != value) ui.spinboxContrast->setValue(value); });
    connect(ui.spinboxContrast, &QSpinBox::valueChanged, this, [this](int value) {
            if (ui.kcfg_Contrast->value() != value) ui.kcfg_Contrast->setValue(value); });
}

void BlurEffectConfig::setupConstraints() {
#if defined(BBDX_X11)
    /**
     * X11 only has one mode available
     */
    ui.kcfg_BlitMode->setEnabled(false);
#endif

    // wallpaper mode expects the cache
    // and doesn't care about the flush interval
    auto slotBlitModeChanged = [this](int index) {
        switch (static_cast<BBDX::BlitMode>(index)) {
            case BBDX::BlitMode::WALLPAPER:
                ui.kcfg_BlurCacheIgnore->setEnabled(false);
                ui.kcfg_BlurCacheRateLimit->setEnabled(false);
                break;

            default:
                ui.kcfg_BlurCacheIgnore->setEnabled(true);
                ui.kcfg_BlurCacheRateLimit->setEnabled(true);
                break;
        }
    };
    connect(ui.kcfg_BlitMode, &QComboBox::currentIndexChanged, this, slotBlitModeChanged);
    slotBlitModeChanged(ui.kcfg_BlitMode->currentIndex());
}

void BlurEffectConfig::slotRefractionModeChanged(int index) {
    // 1 = concave
    // TODO: make this an enum
    const bool concave{index == 1};

    // Edge behaviour is not relevant for concave mode
    if (ui.kcfg_RefractionTextureRepeatMode) {
        ui.kcfg_RefractionTextureRepeatMode->setEnabled(!concave);
    }
    if (ui.labelRefractionTextureRepeatMode) {
        ui.labelRefractionTextureRepeatMode->setEnabled(!concave);
    }

    // Corner radius is only relevant for Concave as Basic breaks with low values
    if (ui.kcfg_RefractionCornerRadius) {
        ui.kcfg_RefractionCornerRadius->setEnabled(concave);
    }
    if (ui.labelRefractionCornerRadius) {
        ui.labelRefractionCornerRadius->setEnabled(concave);
    }
    if (ui.sliderLabelRefractionCornerRadiusSquare) {
        ui.sliderLabelRefractionCornerRadiusSquare->setEnabled(concave);
    }
    if (ui.sliderLabelRefractionCornerRadiusRound) {
        ui.sliderLabelRefractionCornerRadiusRound->setEnabled(concave);
    }
}

void BlurEffectConfig::save() {
    KCModule::save();

    OrgKdeKwinEffectsInterface interface(QStringLiteral("org.kde.KWin"),
                                         QStringLiteral("/Effects"),
                                         QDBusConnection::sessionBus());

    interface.reconfigureEffect(QStringLiteral("better_blur_dx"));
}

} // namespace BBDX

#include "blur_config.moc"

#include "moc_blur_config.cpp"
