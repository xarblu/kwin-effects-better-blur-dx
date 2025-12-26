#include "window_matcher.hpp"

#include "blurconfig.h"
#include "utils.h"

#include <effect/effectwindow.h>
#include <window.h>

#include <QList>
#include <QString>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QLoggingCategory>

#include <utility>

Q_LOGGING_CATEGORY(WINDOW_MATCHER, "kwin_effect_better_blur_dx.window_matcher", QtWarningMsg)

namespace KWin
{

WindowMatcher::WindowMatcher (BlurConfig *config) {
    if (!config) {
        qCWarning(WINDOW_MATCHER) << BBDX_LOG_PREFIX
                                  << "WindowMatcher constructor called before BlurConfig::read()";
        return;
    }

    QList<QString> windowClassesFixed{};
    QList<QRegularExpression> windowClassesRegex{};

    for (const auto &line : config->windowClasses().split("\n", Qt::SkipEmptyParts)) {
        if (line.length() >= 2 && line.startsWith(QChar('/')) && line.endsWith(QChar('/'))) {
            // regex pattern

            QString pattern = line.sliced(1, line.length() - 2);
            QRegularExpression regex{pattern};

            if (!regex.isValid()) {
                qCWarning(WINDOW_MATCHER) << BBDX_LOG_PREFIX
                                          << "Ignoring malformed regex pattern:" << pattern
                                          << "-" << regex.errorString();
                continue;
            }

            regex.optimize();
            windowClassesRegex.append(std::move(regex));

        } else {
            // fixed string
            // $blank -> empty window class

            if (line == QStringLiteral("$blank")) {
                windowClassesFixed.append(QStringLiteral(""));
            } else {
                windowClassesFixed.append(line);
            }
        }
    }

    setWindowClassesFixed(std::move(windowClassesFixed));
    setWindowClassesRegex(std::move(windowClassesRegex));

    if (config->blurMatching()) {
        setWindowClassMatchMode(WindowMatcher::Mode::Whitelist);
    } else {
        setWindowClassMatchMode(WindowMatcher::Mode::Blacklist);
    }

    setMatchMenus(config->blurMenus());
    setMatchDocks(config->blurDocks());
}

bool WindowMatcher::ignoreWindow(const EffectWindow *w) {
    if (w->isDesktop())
        return true;

    if (!m_matchMenus && isMenu(w))
        return true;

    if (!m_matchDocks && w->isDock())
        return true;

    const QString windowClass = w->window()->resourceClass();
    const Layer layer = w->window()->layer();

    if (windowClass == QStringLiteral("xwaylandvideobridge"))
        return true;

    if ((windowClass == "spectacle" || windowClass == "org.kde.spectacle")
        && (layer == Layer::OverlayLayer || layer == Layer::ActiveLayer))
        return true;

    return false;
}

bool WindowMatcher::matchFixed(const EffectWindow *w) {
    if (m_windowClassesFixed.contains(w->window()->resourceClass()))
        return true;

    if (m_windowClassesFixed.contains(w->window()->resourceName()))
        return true;

    return false;
}

bool WindowMatcher::matchRegex(const EffectWindow *w) {
    for (const auto &regex : m_windowClassesRegex) {
        if (auto m = regex.match(w->window()->resourceClass()); m.hasMatch())
            return true;

        if (auto m = regex.match(w->window()->resourceName()); m.hasMatch())
            return true;
    }

    return false;
}

bool WindowMatcher::match(const EffectWindow *w) {
    if (ignoreWindow(w))
        return false;

    if (matchFixed(w) || matchRegex(w)) {
        switch (m_windowClassMatchMode) {
            case WindowMatcher::Mode::Whitelist:
                return true;
            case WindowMatcher::Mode::Blacklist:
                return false;
        }
    } else {
        switch (m_windowClassMatchMode) {
            case WindowMatcher::Mode::Whitelist:
                return false;
            case WindowMatcher::Mode::Blacklist:
                return true;
        }
    }
}

} // namespace KWin
