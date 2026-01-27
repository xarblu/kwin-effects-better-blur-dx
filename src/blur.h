/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "refraction_pass.hpp"
#include "settings.h"
#include "window_manager.hpp"
#include "kwin_version.hpp"

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80) || defined(BETTERBLUR_X11)
#  include "kwin_compat_6_5.hpp"
#else
#  include <core/rect.h>
#  include <core/region.h>
#endif

#include <effect/effect.h>
#include <effect/effectwindow.h>
#include <opengl/glutils.h>
#include <scene/item.h>
#include <scene/scene.h>
#include <window.h>

#include <QList>

#include <optional>
#include <unordered_map>

namespace KWin
{

class BlurManagerInterface;
class ContrastManagerInterface;

struct BlurRenderData
{
    /// Temporary render targets needed for the Dual Kawase algorithm, the first texture
    /// contains not blurred background behind the window, it's cached.
    std::vector<std::unique_ptr<GLTexture>> textures;
    std::vector<std::unique_ptr<GLFramebuffer>> framebuffers;
};

struct BlurEffectData
{
    /// The region that should be blurred behind the window
    std::optional<Region> content;

    /// The region that should be blurred behind the frame
    std::optional<Region> frame;

    /**
     * The render data per render view, as they can have different
     *  color spaces and even different windows on them
     */
#ifdef BETTERBLUR_X11
    std::unordered_map<Output *, BlurRenderData> render;
#else
    std::unordered_map<RenderView *, BlurRenderData> render;
#endif

    ItemEffect windowEffect;

    std::optional<qreal> brightness;
    std::optional<qreal> contrast;
    std::optional<qreal> saturation;
};

class BlurEffect : public KWin::Effect
{
    Q_OBJECT

public:
    BlurEffect();
    ~BlurEffect() override;

    static bool supported();
    static bool enabledByDefault();

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80) || defined(BETTERBLUR_X11)
    void prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime) override;
#else
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime) override;
#endif
    void drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data) override;

    bool provides(Feature feature) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 20;
    }

    bool eventFilter(QObject *watched, QEvent *event) override;

    bool blocksDirectScanout() const override;

public Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow *w);
    void slotWindowDeleted(KWin::EffectWindow *w);
    void slotWindowWantsBlurRegionUpdate(EffectWindow *w);
#ifdef BETTERBLUR_X11
    void slotScreenRemoved(KWin::Output *view);
#else
    void slotViewRemoved(KWin::RenderView *view);
#endif
    void slotPropertyNotify(KWin::EffectWindow *w, long atom);
    void setupDecorationConnections(EffectWindow *w);

private:
    void initBlurStrengthValues();
    QMatrix4x4 colorMatrix(const BlurEffectData &params) const;
    Region blurRegion(EffectWindow *w) const;
    Region decorationBlurRegion(const EffectWindow *w) const;
    bool decorationSupportsBlurBehind(const EffectWindow *w) const;
    bool shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data);
    void updateBlurRegion(EffectWindow *w);
    void blur(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data);
    GLTexture *ensureNoiseTexture();
    qreal getContrastParam(std::optional<qreal> requested_value, qreal config_value) const;

private:
    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int colorMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
    } m_onscreenPass;

    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int colorMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
        int boxLocation;
        int cornerRadiusLocation;
        int opacityLocation;
    } m_roundedOnscreenPass;

    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
    } m_downsamplePass;

    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
    } m_upsamplePass;

    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int noiseTextureSizeLocation;

        std::unique_ptr<GLTexture> noiseTexture;
        qreal noiseTextureScale = 1.0;
        int noiseTextureStength = 0;
    } m_noisePass;

    bool m_valid = false;
    long net_wm_blur_region = 0;
    Region m_paintedDeviceArea; // keeps track of all painted areas (from bottom to top)
    Region m_currentDeviceBlur; // keeps track of currently blurred area of the windows (from bottom to top)

#ifdef BETTERBLUR_X11
    Output *m_currentView = nullptr;
#else
    RenderView *m_currentView = nullptr;
#endif

    size_t m_iterationCount; // number of times the texture will be downsized to half size
    int m_offset;
    int m_expandSize;
    int m_noiseStrength;

    BlurSettings m_settings;
    BBDX::RefractionPass m_refractionPass{};
    BBDX::WindowManager m_windowManager{};

    struct OffsetStruct
    {
        float minOffset;
        float maxOffset;
        int expandSize;
    };

    QList<OffsetStruct> blurOffsets;

    struct BlurValuesStruct
    {
        int iteration;
        float offset;
    };

    QList<BlurValuesStruct> blurStrengthValues;

    QMap<EffectWindow *, QMetaObject::Connection> windowBlurChangedConnections;
    QMap<EffectWindow *, QMetaObject::Connection> windowContrastChangedConnections;
    std::unordered_map<EffectWindow *, BlurEffectData> m_windows;

    static BlurManagerInterface *s_blurManager;
    static QTimer *s_blurManagerRemoveTimer;

    static ContrastManagerInterface *s_contrastManager;
    static QTimer *s_contrastManagerRemoveTimer;
};

inline bool BlurEffect::provides(Effect::Feature feature)
{
    if (feature == Blur) {
        return true;
    }
    return KWin::Effect::provides(feature);
}

} // namespace KWin
