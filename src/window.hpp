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
    std::optional<QRegion> m_forceBlurContent;
    std::optional<QRegion> m_forceBlurFrame;

private:
    void updateForceBlurRegion();

public Q_SLOTS:
    void slotFrameGeometryChanged();

public:
    explicit Window(KWin::EffectWindow *w);

    /**
     * reconfigure hook
     */
    void reconfigure();

    /**
     * access underlying KWin::EffectWindow
     */
    KWin::EffectWindow* effectwindow() { return m_effectwindow; }
};
} // namespace BBDX
