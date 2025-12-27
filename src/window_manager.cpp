#include "window_manager.hpp"

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

Q_LOGGING_CATEGORY(WINDOW_MANAGER, "kwin_effect_better_blur_dx.window_manager", QtWarningMsg)

namespace KWin
{

WindowManager::WindowManager (BlurConfig *config) {
    if (!config) {
        qCWarning(WINDOW_MANAGER) << BBDX_LOG_PREFIX
                                  << "WindowManager constructor called before BlurConfig::read()";
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
                qCWarning(WINDOW_MANAGER) << BBDX_LOG_PREFIX
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
        setWindowClassMatchMode(WindowManager::WindowClassMatchMode::Whitelist);
    } else {
        setWindowClassMatchMode(WindowManager::WindowClassMatchMode::Blacklist);
    }

    setMatchMenus(config->blurMenus());
    setMatchDocks(config->blurDocks());
}

bool WindowManager::ignoreWindow(const EffectWindow *w) {
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

bool WindowManager::matchFixed(const EffectWindow *w) {
    if (m_windowClassesFixed.contains(w->window()->resourceClass()))
        return true;

    if (m_windowClassesFixed.contains(w->window()->resourceName()))
        return true;

    return false;
}

bool WindowManager::matchRegex(const EffectWindow *w) {
    for (const auto &regex : m_windowClassesRegex) {
        if (auto m = regex.match(w->window()->resourceClass()); m.hasMatch())
            return true;

        if (auto m = regex.match(w->window()->resourceName()); m.hasMatch())
            return true;
    }

    return false;
}

bool WindowManager::match(const EffectWindow *w) {
    if (ignoreWindow(w))
        return false;

    if (matchFixed(w) || matchRegex(w)) {
        switch (m_windowClassMatchMode) {
            case WindowManager::WindowClassMatchMode::Whitelist:
                return true;
            case WindowManager::WindowClassMatchMode::Blacklist:
                return false;
        }
    } else {
        switch (m_windowClassMatchMode) {
            case WindowManager::WindowClassMatchMode::Whitelist:
                return false;
            case WindowManager::WindowClassMatchMode::Blacklist:
                return true;
        }
    }
}

} // namespace KWin
