/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2018 Alex Nemeth <alex.nemeth329@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "blur_cache.hpp"
#include "kwin_version.hpp"
#include "refraction_pass.hpp"
#include "rounded_corners_pass.hpp"
#include "window_manager.hpp"

#include <memory>
#include <opengl/glframebuffer.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
#  include "kwin_compat_6_5.hpp"
#else
#  include <core/rect.h>
#  include <core/region.h>
#endif
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
#  include "kwin_compat_6_6.hpp"
#endif

#include <effect/effect.h>
#include <effect/effectwindow.h>
#include <opengl/glutils.h>
#include <scene/item.h>
#include <scene/scene.h>
#if KWIN_VERSION >= KWIN_VERSION_CODE(6, 6, 4)
#include <scene/backgroundeffectitem.h>
#endif
#include <window.h>

#include <QList>

#include <optional>
#include <unordered_map>

namespace KWin {
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
class BlurManagerInterface;
class ContrastManagerInterface;
#endif
}

namespace BBDX {
using namespace KWin;

class BlurCacheLRU;

struct BlurRenderData
{
    /// Temporary render targets needed for the Dual Kawase algorithm, the first texture
    /// contains not blurred background behind the window, it's cached.
    std::vector<std::unique_ptr<GLTexture>> textures;
    std::vector<std::unique_ptr<GLFramebuffer>> framebuffers;

    BBDX::BlurCacheLRU cache;
};

struct BlurEffectData
{
    /// The region that should be blurred behind the window
    std::optional<RegionF> content;

    /// The region that should be blurred behind the frame
    std::optional<RegionF> frame;

    /**
     * The render data per render view, as they can have different
     *  color spaces and even different windows on them
     */
#if defined(BETTERBLUR_X11)
    std::unordered_map<Output *, BlurRenderData> render;
#else
    std::unordered_map<RenderView *, BlurRenderData> render;
#endif

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 4)
    ItemEffect windowEffect;
#else
    std::unique_ptr<BackgroundEffectItem> blurItem;
#endif

    /**
     * Color transformation matrix (brightness, contrast, and saturation).
     */
    std::optional<QMatrix4x4> colorMatrix;
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
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
#else
    void prePaintScreen(ScreenPrePaintData &data) override;
#endif
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
    void prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime) override;
#elif KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime) override;
#else
    void prePaintWindow(RenderView *view, EffectWindow *w, WindowPrePaintData &data) override;
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
#if defined(BETTERBLUR_X11)
    void slotScreenRemoved(KWin::Output *view);
#else
    void slotViewRemoved(KWin::RenderView *view);
#endif
    void slotPropertyNotify(KWin::EffectWindow *w, long atom);
    void setupDecorationConnections(EffectWindow *w);

private:
    void initBlurStrengthValues();
    RegionF blurRegion(EffectWindow *w) const;
    RegionF decorationBlurRegion(const EffectWindow *w) const;
    bool decorationSupportsBlurBehind(const EffectWindow *w) const;
    bool shouldBlur(const EffectWindow *w, int mask, const WindowPaintData &data) const;
    void updateBlurRegion(EffectWindow *w);
    void blur(const RenderTarget &renderTarget, const RenderViewport &viewport, EffectWindow *w, int mask, const Region &deviceRegion, WindowPaintData &data);
    GLTexture *ensureNoiseTexture();

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
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 4)
    Region m_paintedDeviceArea; // keeps track of all painted areas (from bottom to top)
    Region m_currentDeviceBlur; // keeps track of currently blurred area of the windows (from bottom to top)
#endif

#if defined(BETTERBLUR_X11)
    Output *m_currentView = nullptr;
#else
    RenderView *m_currentView = nullptr;
#endif

    QMatrix4x4 m_colorMatrix;
    size_t m_iterationCount; // number of times the texture will be downsized to half size
    int m_offset;
    int m_expandSize;
    int m_noiseStrength;

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
#if !defined(BETTERBLUR_X11) && KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
    QMap<EffectWindow *, QMetaObject::Connection> windowContrastChangedConnections;
#endif
    std::unordered_map<EffectWindow *, BlurEffectData> m_windows;

#if !defined(BETTERBLUR_X11) && KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
    static BlurManagerInterface *s_blurManager;
    static QTimer *s_blurManagerRemoveTimer;

    static ContrastManagerInterface *s_contrastManager;
    static QTimer *s_contrastManagerRemoveTimer;
#endif

    // BBDX Mixins
    bool m_forceContrastParams{false};

    std::unique_ptr<BBDX::WindowManager> m_windowManager{};
    friend void BBDX::WindowManager::triggerBlurRegionUpdate(KWin::EffectWindow *w) const;
    friend void BBDX::WindowManager::invalidateBlurCache(KWin::EffectWindow *w, QStringView reason) const;
    friend bool BBDX::WindowManager::windowBlurIsFullyCovered(KWin::EffectWindow *w) const;
    std::unique_ptr<BBDX::BlurCache> m_blurCache{};
    std::unique_ptr<BBDX::RefractionPass> m_refractionPass{};
    std::unique_ptr<BBDX::RoundedCornersPass> m_roundedCornersPass{};

public:
    WindowManager* windowManager() const { return m_windowManager.get(); }
    BlurCache* blurCache() const { return m_blurCache.get(); }
};

inline bool BlurEffect::provides(Effect::Feature feature)
{
    if (feature == Blur) {
        return true;
    }
    return KWin::Effect::provides(feature);
}

} // namespace BBDX
