#pragma once

#include <QObject>
#include <QRegion>

#include <optional>

namespace KWin {
    class EffectWindow;
}

namespace BBDX {

class Window : public QObject {
    Q_OBJECT

private:
    KWin::EffectWindow* m_effectwindow;

    // track whether this window should be force blurred
    bool m_forceBlurred{false};

    // if force blurred, contain content/frame of the blur region
    std::optional<QRegion> m_forceBlurContent{};
    std::optional<QRegion> m_forceBlurFrame{};

private:
    void updateForceBlurRegion();
    void triggerBlurRegionUpdate();

public Q_SLOTS:
    void slotWindowFrameGeometryChanged();
    void slotWindowFinishUserMovedResized();

public:
    explicit Window(KWin::EffectWindow *w);

    /**
     * getters
     */
    std::optional<QRegion> forceBlurContent() const { return m_forceBlurContent; };
    std::optional<QRegion> forceBlurFrame() const { return m_forceBlurFrame; };

    /**
     * reconfigure hook
     */
    void reconfigure();

    /**
     * access underlying KWin::EffectWindow
     */
    const KWin::EffectWindow* effectwindow() { return m_effectwindow; }
};
} // namespace BBDX
