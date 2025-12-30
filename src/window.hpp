#pragma once

#include <QObject>
#include <QRegion>

#include <optional>
#include <chrono>

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
    std::optional<QRegion> m_forceBlurContent{};
    std::optional<QRegion> m_forceBlurFrame{};

    // track whether this window requested a blur region
    bool m_requestedBlur{false};

    // track mazimized state
    MaximizedState m_maximizedState{MaximizedState::Unknown};

    // track whether window should be blurred even
    // when PAINT_WINDOW_TRANSFORMED is set
    bool m_shouldBlurWhileTransformed{false};
    TransformState m_blurWhileTransformedTransitionState{TransformState::None};
    std::chrono::steady_clock::time_point m_blurWhileTransformedTransitionStart;

private:
    void updateForceBlurRegion();
    void triggerBlurRegionUpdate();

public Q_SLOTS:
    void slotWindowFrameGeometryChanged();
    void slotWindowMaximizedStateChanged(bool horizontal, bool vertical);
    void slotWindowStartUserMovedResized();
    void slotWindowFinishUserMovedResized();

public:
    explicit Window(WindowManager *wm, KWin::EffectWindow *w);

    /**
     * setters
     */
    void setRequestedBlur(bool toggle) { m_requestedBlur = toggle; }

    /**
     * getters
     */
    KWin::EffectWindow* effectwindow() const { return m_effectwindow; }
    std::optional<QRegion> forceBlurContent() const { return m_forceBlurContent; };
    std::optional<QRegion> forceBlurFrame() const { return m_forceBlurFrame; };
    bool requestedBlur() const { return m_requestedBlur; };
    bool forceBlurred() const { return m_shouldForceBlur && !m_requestedBlur; }
    bool shouldBlurWhileTransformed() const { return m_shouldBlurWhileTransformed; }

    /**
     * reconfigure hook
     */
    void reconfigure();

    /**
     * Get the final blur region written into
     * the provided content/frame references
     *
     * If values already exists keeps them and sets
     * m_requestedBlur flag, else writes the current force blur region
     */
    void getFinalBlurRegion(std::optional<QRegion> &content, std::optional<QRegion> &frame);

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
};

} // namespace BBDX
