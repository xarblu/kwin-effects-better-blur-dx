#pragma once

#include "blurconfig.h"
#include "window.hpp"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QObject>
#include <QList>
#include <QRegularExpression>
#include <QMap>
#include <QString>

#include <optional>
#include <utility>

namespace BBDX
{

class WindowManager : public QObject {
    Q_OBJECT

public:
    enum class WindowClassMatchMode {
        Whitelist,
        Blacklist,
    };

private:
    QMap<KWin::EffectWindow *, BBDX::Window *> m_windows{};

    // window classes
    QList<QString> m_windowClassesFixed{};
    QList<QRegularExpression> m_windowClassesRegex{};
    WindowClassMatchMode m_windowClassMatchMode{WindowClassMatchMode::Whitelist};

    // window types
    bool m_matchMenus{false};
    bool m_matchDocks{false};

    // match helpers
    bool ignoreWindow(const KWin::EffectWindow *w);
    bool matchFixed(const KWin::EffectWindow *w);
    bool matchRegex(const KWin::EffectWindow *w);

public Q_SLOT:
    void slotWindowAdded(KWin::EffectWindow *w);
    void slotWindowDeleted(KWin::EffectWindow *w);

public:
    explicit WindowManager();

    /**
     * access to singleton
     */
    static const WindowManager *instance();

    /**
     * reconfigure from BlurConfig
     */
    void reconfigure();

    void setWindowClassesFixed(QList<QString> windowClasses) {
        m_windowClassesFixed = std::move(windowClasses);
    }
    void setWindowClassesRegex(QList<QRegularExpression> windowClasses) {
        m_windowClassesRegex = std::move(windowClasses);
    }
    void setWindowClassMatchMode(WindowClassMatchMode mode) {
        m_windowClassMatchMode = mode;
    }
    void setMatchMenus(bool match) {
        m_matchMenus = match;
    }
    void setMatchDocks(bool match) {
        m_matchDocks = match;
    }

    /**
     * Find a managed window, nullptr if not found
     */
    BBDX::Window* findWindow(KWin::EffectWindow *w);

    /**
     * Match an EffectWindow instance
     */
    bool match(const KWin::EffectWindow *w);
};

} // namespace KWin
