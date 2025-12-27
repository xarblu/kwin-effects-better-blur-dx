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

    std::optional<QRegion> m_forceBlurContent;
    std::optional<QRegion> m_forceBlurFrame;

    bool m_forceBlurred{false};

private:
    void updateForceBlurRegion();

public Q_SLOTS:
    void slotFrameGeometryChanged(KWin::EffectWindow *w);

public:
    explicit Window(KWin::EffectWindow *w);

    /**
     * access underlying KWin::EffectWindow
     */
    KWin::EffectWindow* effectwindow() { return m_effectwindow; }
};
} // namespace BBDX
