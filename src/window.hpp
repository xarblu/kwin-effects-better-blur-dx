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

    // track whether this window requested a blur region
    bool m_requestedBlur{false};

private:
    void updateForceBlurRegion();
    void triggerBlurRegionUpdate();

public Q_SLOTS:
    void slotWindowFrameGeometryChanged();

public:
    explicit Window(KWin::EffectWindow *w);

    /**
     * setters
     */
    void setRequestedBlur(bool toggle) { m_requestedBlur = toggle; }

    /**
     * getters
     */
    KWin::EffectWindow* effectwindow() const { return m_effectwindow; }
    bool forceBlurred() const { return m_forceBlurred; }
    std::optional<QRegion> forceBlurContent() const { return m_forceBlurContent; };
    std::optional<QRegion> forceBlurFrame() const { return m_forceBlurFrame; };
    bool requestedBlur() const { return m_requestedBlur; };

    /**
     * reconfigure hook
     */
    void reconfigure();
};

} // namespace BBDX
