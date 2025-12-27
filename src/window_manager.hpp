#pragma once

#include "blurconfig.h"

#include <effect/effectwindow.h>

#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>

#include <utility>

namespace KWin
{

class WindowManager {
public:
    enum class WindowClassMatchMode {
        Whitelist,
        Blacklist,
    };

private:
    // window classes
    QList<QString> m_windowClassesFixed{};
    QList<QRegularExpression> m_windowClassesRegex{};
    WindowClassMatchMode m_windowClassMatchMode{WindowClassMatchMode::Whitelist};

    // window types
    bool m_matchMenus{false};
    bool m_matchDocks{false};

    // match sets used for a) caching and b) tracking
    // allowing full "groups" of related windows to be matched
    // by the same window class
    QSet<EffectWindow *> m_matched{};
    QSet<EffectWindow *> m_not_matched{};

    // match helpers
    bool ignoreWindow(const EffectWindow *w);
    bool matchFixed(const EffectWindow *w);
    bool matchRegex(const EffectWindow *w);

public:
    WindowManager() = default;

    /**
     * Construct from the BlurConfig singleton accessed via BlurConfig::self()
     */
    WindowManager(BlurConfig *config);

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
     * Match an EffectWindow instance
     */
    bool match(const EffectWindow *w);
};

} // namespace KWin
