/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "effect/effect.h"
#include "opengl/glutils.h"

#include "scene/item.h"
#include "scene/scene.h"

#include "settings.h"
#include "window_manager.hpp"
#include "window.h"

#include <QList>

#include <effect/effectwindow.h>
#include <optional>
#include <unordered_map>

namespace KWin
{

class BlurManagerInterface;
class ContrastManagerInterface;

enum class BlurType {
    Unknown,
    Requested,
    Forced
};

enum class MaximizedState {
    Unknown,
    Restored,
    Horizontal,
    Vertical,
    Complete
};

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
    std::optional<QRegion> content;

    /// The region that should be blurred behind the frame
    std::optional<QRegion> frame;

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

    BlurType type = BlurType::Unknown;

    MaximizedState maximizedState = MaximizedState::Unknown;
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
    void prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void drawWindow(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data) override;

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
    void slotWindowMaximizedStateChanged(EffectWindow *w, bool horizontal, bool vertical);
#ifdef BETTERBLUR_X11
    void slotScreenRemoved(KWin::Output *view);
#else
    void slotViewRemoved(KWin::RenderView *view);
#endif
#ifdef BETTERBLUR_X11
    void slotPropertyNotify(KWin::EffectWindow *w, long atom);
#endif
    void setupDecorationConnections(EffectWindow *w);
    void slotWindowWantsBlurRegionUpdate(EffectWindow *w);

private:
    void initBlurStrengthValues();
    QMatrix4x4 colorMatrix(const BlurEffectData &params) const;
    QRegion blurRegion(EffectWindow *w) const;
    QRegion decorationBlurRegion(const EffectWindow *w) const;
    bool decorationSupportsBlurBehind(const EffectWindow *w) const;
    bool shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data);
    void updateBlurRegion(EffectWindow *w, bool geometryChanged = false);
    void updateForceBlurRegion(const EffectWindow *w, std::optional<QRegion> &content, std::optional<QRegion> &frame, BlurType &type);
    void blur(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data);
    GLTexture *ensureNoiseTexture();
    BorderRadius getWindowBorderRadius(EffectWindow *w);
    qreal getContrastParam(std::optional<qreal> requested_value, qreal config_value) const;
    qreal getOpacity(const EffectWindow *w, WindowPaintData &data, BlurEffectData &blurInfo) const;

private:
    struct
    {
        std::unique_ptr<GLShader> shader;
        int mvpMatrixLocation;
        int colorMatrixLocation;
        int offsetLocation;
        int halfpixelLocation;
    } m_contrastPass;

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
    } m_roundedContrastPass;

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
#ifdef BETTERBLUR_X11
    long net_wm_blur_region = 0;
#endif
    QRegion m_paintedArea; // keeps track of all painted areas (from bottom to top)
    QRegion m_currentBlur; // keeps track of currently blurred area of the windows (from bottom to top)

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

    // Windows to blur even when transformed.
    QList<const EffectWindow*> m_blurWhenTransformed;

    QMap<EffectWindow *, QMetaObject::Connection> windowBlurChangedConnections;
    QMap<EffectWindow *, QMetaObject::Connection> windowContrastChangedConnections;
    QMap<EffectWindow *, QMetaObject::Connection> windowFrameGeometryChangedConnections;
    QMap<EffectWindow *, QMetaObject::Connection> windowMaximizedStateChangedConnections;
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
