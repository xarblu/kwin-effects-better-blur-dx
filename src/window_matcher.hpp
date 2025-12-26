#pragma once

#include "blurconfig.h"

#include <effect/effectwindow.h>

#include <QList>
#include <QRegularExpression>
#include <QString>

#include <utility>

namespace KWin
{

class WindowMatcher {
public:
    enum class Mode {
        Whitelist,
        Blacklist,
    };

private:
    // window classes
    QList<QString> m_windowClassesFixed{};
    QList<QRegularExpression> m_windowClassesRegex{};
    Mode m_windowClassMatchMode{Mode::Whitelist};

    // window types
    bool m_matchMenus{false};
    bool m_matchDocks{false};

    // match helpers
    bool ignoreWindow(EffectWindow *w);
    bool matchFixed(EffectWindow *w);
    bool matchRegex(EffectWindow *w);

public:
    WindowMatcher() = default;

    /**
     * Construct from the BlurConfig singleton accessed via BlurConfig::self()
     */
    WindowMatcher(BlurConfig *config);

    void setWindowClassesFixed(QList<QString> windowClasses) {
        m_windowClassesFixed = std::move(windowClasses);
    }
    void setWindowClassesRegex(QList<QRegularExpression> windowClasses) {
        m_windowClassesRegex = std::move(windowClasses);
    }
    void setWindowClassMatchMode(Mode mode) {
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
    bool match(EffectWindow *w);
};

} // namespace KWin
