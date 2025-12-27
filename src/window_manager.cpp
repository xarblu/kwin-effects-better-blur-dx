#include "window_manager.hpp"

#include "blurconfig.h"
#include "utils.h"
#include "window.hpp"

#include <effect/effectwindow.h>
#include <effect/effecthandler.h>
#include <window.h>

#include <QList>
#include <QMap>
#include <QString>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QLoggingCategory>

#include <utility>

Q_LOGGING_CATEGORY(WINDOW_MANAGER, "kwin_effect_better_blur_dx.window_manager", QtWarningMsg)

namespace BBDX
{

static const WindowManager *self;

WindowManager::WindowManager() {
    self = this;

    // add existing windows
    for (const auto &window : KWin::effects->stackingOrder()) {
        slotWindowAdded(window);
    }

    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &WindowManager::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, &WindowManager::slotWindowDeleted);
}

const WindowManager* WindowManager::instance() {
    return self;
}

void WindowManager::slotWindowAdded(KWin::EffectWindow *w) {
    m_windows[w] = new BBDX::Window(w);
}

void WindowManager::slotWindowDeleted(KWin::EffectWindow *w) {
    if (BBDX::Window* bbdx_window = findWindow(w)) {
        delete bbdx_window;
        m_windows.remove(w);
    }
}

BBDX::Window* WindowManager::findWindow(KWin::EffectWindow *w) {
    if (const auto it = m_windows.find(w); it != m_windows.end()) {
        return it.value();
    }
    return nullptr;
}

void WindowManager::reconfigure() {
    auto config = KWin::BlurConfig::self();

    if (!config) {
        qCWarning(WINDOW_MANAGER) << BBDX::LOG_PREFIX
                                  << "WindowManager::reconfigure() called before BlurConfig::read()";
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
                qCWarning(WINDOW_MANAGER) << BBDX::LOG_PREFIX
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

bool WindowManager::ignoreWindow(const KWin::EffectWindow *w) {
    if (w->isDesktop())
        return true;

    if (!m_matchMenus && isMenu(w))
        return true;

    if (!m_matchDocks && w->isDock())
        return true;

    const QString windowClass = w->window()->resourceClass();
    const KWin::Layer layer = w->window()->layer();

    if (windowClass == QStringLiteral("xwaylandvideobridge"))
        return true;

    if ((windowClass == "spectacle" || windowClass == "org.kde.spectacle")
        && (layer == KWin::Layer::OverlayLayer || layer == KWin::Layer::ActiveLayer))
        return true;

    return false;
}

bool WindowManager::matchFixed(const KWin::EffectWindow *w) {
    if (m_windowClassesFixed.contains(w->window()->resourceClass()))
        return true;

    if (m_windowClassesFixed.contains(w->window()->resourceName()))
        return true;

    return false;
}

bool WindowManager::matchRegex(const KWin::EffectWindow *w) {
    for (const auto &regex : m_windowClassesRegex) {
        if (auto m = regex.match(w->window()->resourceClass()); m.hasMatch())
            return true;

        if (auto m = regex.match(w->window()->resourceName()); m.hasMatch())
            return true;
    }

    return false;
}

bool WindowManager::match(const KWin::EffectWindow *w) {
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
