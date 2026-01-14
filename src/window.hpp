#pragma once

#include "kwin_version.hpp"

#include <QObject>
#include <QRegion>

#include <chrono>
#include <optional>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
#  include "kwin_compat_6_6.hpp"
#else
#  include <core/region.h>
#endif

namespace KWin {
    class BorderRadius;
    class EffectWindow;
    class WindowPaintData;
}

namespace BBDX {

class WindowManager;

class Window : public QObject {
    Q_OBJECT

public:
    enum class MaximizedState {
        Unknown,
        Restored,
        Vertical,
        Horizontal,
        Complete
    };

    enum class TransformState {
        None,
        Started,
        Ended
    };

    enum class BlurOrigin : unsigned int {
        RequestedContent = 1 << 0,
        RequestedFrame   = 1 << 1,
        ForcedContent    = 1 << 2,
        ForcedFrame      = 1 << 3,
    };

private:
    // managing WindowManager instance
    WindowManager* m_windowManager;

    // underlying KWin::EffectWindow
    KWin::EffectWindow* m_effectwindow;

    // User config related attributes
    // track whether this window should be force blurred
    bool m_shouldForceBlur{false};
    qreal m_userBorderRadius{0.0};

    // if force blurred, contain content/frame of the blur region
    std::optional<KWin::Region> m_forceBlurContent{};
    std::optional<KWin::Region> m_forceBlurFrame{};

    // track whether this window requested a blur region
    unsigned int m_blurOriginMask{0};

    // track mazimized state
    MaximizedState m_maximizedState{MaximizedState::Unknown};
    bool m_minimizedFromMaximized{false};

    // track whether window is currently being transformed
    bool m_isTransformed{false};

    // track whether window should be blurred even
    // when PAINT_WINDOW_TRANSFORMED is set
    bool m_shouldBlurWhileTransformed{false};
    TransformState m_blurWhileTransformedTransitionState{TransformState::None};
    std::chrono::steady_clock::time_point m_blurWhileTransformedTransitionStart;

private:
    void updateForceBlurRegion();
    void triggerBlurRegionUpdate();

public Q_SLOTS:
    void slotMinimizedChanged();
    void slotWindowFrameGeometryChanged();
    void slotWindowMaximizedStateChanged(bool horizontal, bool vertical);
    void slotWindowStartUserMovedResized();
    void slotWindowFinishUserMovedResized();

public:
    explicit Window(WindowManager *wm, KWin::EffectWindow *w);

    /**
     * setters
     */
    void setIsTransformed(bool toggle);

    /**
     * getters
     */
    KWin::EffectWindow* effectwindow() const { return m_effectwindow; }
    std::optional<KWin::Region> forceBlurContent() const { return m_forceBlurContent; };
    std::optional<KWin::Region> forceBlurFrame() const { return m_forceBlurFrame; };
    bool shouldBlurWhileTransformed() const;

    /**
     * reconfigure hook
     */
    void reconfigure();

    /**
     * Get the final blur region written into
     * the provided content/frame references
     *
     * If values already exists keeps them and sets
     * m_blurOriginMask appropriately, else writes the current force blur region
     */
    void getFinalBlurRegion(std::optional<KWin::Region> &content, std::optional<KWin::Region> &frame);

    /**
     * Get the effective border radius
     *
     * For blur-requested windows this respects their radius.
     * Otherwise the configured radius is used.
     * 
     * If the window is fullscreen/maximized the radius is always 0.
     */
    KWin::BorderRadius getEffectiveBorderRadius();

    /**
     * Get effective blur opacity
     */
    qreal getEffectiveBlurOpacity(KWin::WindowPaintData &data);

    /**
     * Check if this window (likely) is a Plasma surface that should
     * get special treatment like non-rounded corners
     */
    bool isPlasmaSurface() const;
};

} // namespace BBDX
