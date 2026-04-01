#include "window_manager.hpp"

#include "blurconfig.h"
#include "kwin_version.hpp"
#include "utils.h"
#include "window.hpp"

#include <effect/effectwindow.h>
#include <effect/effecthandler.h>
#include <scene/borderradius.h>
#include <window.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80) || defined(BETTERBLUR_X11)
#  include "kwin_compat_6_5.hpp"
#else
#  include <core/output.h>
#  include <core/region.h>
#endif

#include <QList>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>

#include <memory>
#include <utility>

Q_LOGGING_CATEGORY(WINDOW_MANAGER, "kwin_effect_better_blur_dx.window_manager", QtInfoMsg)

static const BBDX::WindowManager *self;

BBDX::WindowManager::WindowManager() {
    self = this;

    // add existing windows
    for (const auto &window : KWin::effects->stackingOrder()) {
        slotWindowAdded(window);
    }

    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &WindowManager::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, &WindowManager::slotWindowDeleted);
}

const BBDX::WindowManager* BBDX::WindowManager::instance() {
    return self;
}

void BBDX::WindowManager::slotWindowAdded(KWin::EffectWindow *w) {
    auto window = std::make_unique<BBDX::Window>(this, w);

    qCDebug(WINDOW_MANAGER) << BBDX::LOG_PREFIX << "Window added:" << *window;

    m_windows.insert_or_assign(w, std::move(window));

    if (w->isDock()) {
        m_docks.insert(w);
        refreshMaximizedStateAll();
    }
}

void BBDX::WindowManager::slotWindowDeleted(KWin::EffectWindow *w) {
    if (const auto it = m_windows.find(w); it != m_windows.end()) {
        qCDebug(WINDOW_MANAGER) << BBDX::LOG_PREFIX << "Window removed:" << *(it->second);
        m_windows.erase(it);
    }

    if (const auto it = m_docks.find(w); it != m_docks.end()) {
        m_docks.erase(it);
        refreshMaximizedStateAll();
    }
}

BBDX::Window* BBDX::WindowManager::findWindow(const KWin::EffectWindow *w) const {
    if (const auto it = m_windows.find(w); it != m_windows.end()) {
        return it->second.get();
    }
    return nullptr;
}

void BBDX::WindowManager::reconfigure() {
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

    m_windowClassesFixed = std::move(windowClassesFixed);
    m_windowClassesRegex = std::move(windowClassesRegex);

    if (config->blurMatching()) {
        m_windowClassMatchMode = WindowClassMatchMode::Whitelist;
    } else {
        m_windowClassMatchMode = WindowClassMatchMode::Blacklist;
    }

    m_blurDecorations = config->blurDecorations();
    m_blurDocks = config->blurDocks();
    m_blurMenus = config->blurMenus();

    m_userBorderRadius = config->cornerRadius();

    for (const auto &[_, window] : m_windows) {
        window->reconfigure();
    }
}

void BBDX::WindowManager::refreshMaximizedState(BBDX::Window *window) const {
    const KWin::EffectWindow *w = window->effectwindow();

    const KWin::LogicalOutput* screen = w->screen();

    KWin::Region effectiveScreenRegion = KWin::Region(screen->geometry());

    for (const auto &dock : m_docks) {
        if (dock->screen() != screen)  {
            continue;
        }

        effectiveScreenRegion -= KWin::Region(KWin::Rect(dock->frameGeometry().toRect()));
    }

    const KWin::Rect effectiveScreenRect{effectiveScreenRegion.boundingRect()};
    const KWin::Rect windowRect{KWin::Rect(w->frameGeometry().toRect())};

    bool maximizedHorizontal{false};
    bool maximizedVertical{false};
    if (windowRect.left() <= effectiveScreenRect.left()
        && windowRect.right() >= effectiveScreenRect.right()) {
        maximizedHorizontal = true;
    }

    if (windowRect.top() <= effectiveScreenRect.top()
        && windowRect.bottom() >= effectiveScreenRect.bottom()) {
        maximizedVertical = true;
    }

    if (maximizedHorizontal && maximizedVertical) {
        window->setMaximizedState(Window::MaximizedState::Complete);
    } else if (maximizedHorizontal && !maximizedVertical) {
        window->setMaximizedState(Window::MaximizedState::Horizontal);
    } else if (!maximizedHorizontal && maximizedVertical) {
        window->setMaximizedState(Window::MaximizedState::Vertical);
    } else {
        window->setMaximizedState(Window::MaximizedState::Restored);
    }
}

void BBDX::WindowManager::refreshMaximizedStateAll() const {
    for (const auto &[w, window] : m_windows) {
        refreshMaximizedState(window.get());
    }
}

bool BBDX::WindowManager::matchesWindowClassFixed(const KWin::EffectWindow *w) const {
    if (m_windowClassesFixed.contains(w->window()->resourceClass()))
        return true;

    if (m_windowClassesFixed.contains(w->window()->resourceName()))
        return true;

    return false;
}

bool BBDX::WindowManager::matchesWindowClassRegex(const KWin::EffectWindow *w) const {
    for (const auto &regex : m_windowClassesRegex) {
        if (auto m = regex.match(w->window()->resourceClass()); m.hasMatch())
            return true;

        if (auto m = regex.match(w->window()->resourceName()); m.hasMatch())
            return true;
    }

    return false;
}

bool BBDX::WindowManager::shouldForceBlurWindowClass(const KWin::EffectWindow *w) const {
    if (matchesWindowClassFixed(w) || matchesWindowClassRegex(w)) {
        switch (m_windowClassMatchMode) {
            case WindowManager::WindowClassMatchMode::Whitelist:
                return true;
            case WindowManager::WindowClassMatchMode::Blacklist:
                return false;
            [[unlikely]] default:
                return false;
        }
    } else {
        switch (m_windowClassMatchMode) {
            case WindowManager::WindowClassMatchMode::Whitelist:
                return false;
            case WindowManager::WindowClassMatchMode::Blacklist:
                return true;
            [[unlikely]] default:
                return false;
        }
    }
}

void BBDX::WindowManager::triggerBlurRegionUpdate(KWin::EffectWindow *w) const {
    emit windowWantsBlurRegionUpdate(w);
}

void BBDX::WindowManager::setWindowIsTransformed(const KWin::EffectWindow *w, bool toggle) const {
    const auto window = findWindow(w);

    if (!window)
        return;

    window->setIsTransformed(toggle);
}

bool BBDX::WindowManager::windowShouldBlurWhileTransformed(const KWin::EffectWindow *w) const {
    const auto window = findWindow(w);

    // assume false for unmanaged windows
    if (!window)
        return false;

    return window->shouldBlurWhileTransformed();
}

void BBDX::WindowManager::getFinalBlurRegion(const KWin::EffectWindow *w, std::optional<KWin::Region> &content, std::optional<KWin::Region> &frame) const {
    const auto window = findWindow(w);
    if (!window)
        return;

    window->getFinalBlurRegion(content, frame);
}

KWin::BorderRadius BBDX::WindowManager::getEffectiveBorderRadius(const KWin::EffectWindow *w) const {
    const auto window = findWindow(w);

    // unmanaged windows can never be force blurred
    if (!window)
        return KWin::BorderRadius();

    return window->getEffectiveBorderRadius();
}

qreal BBDX::WindowManager::getEffectiveBlurOpacity(const KWin::EffectWindow *w, KWin::WindowPaintData &data) const {
    const auto window = findWindow(w);

    // for unmanaged windows just use the paint data
    if (!window)
        return data.opacity();

    return window->getEffectiveBlurOpacity(data);
}

void BBDX::WindowManager::repaintBlurredWindowsAbove(const KWin::EffectWindow *w) const {
    const auto stackingOrder = KWin::effects->stackingOrder();

    auto it = stackingOrder.begin();
    // find next window in stacking order
    for (; it != stackingOrder.end(); it++) {
        if (*it == w) {
            it++;
            break;
        }
    }

    // repaint windows above, if any
    for (; it != stackingOrder.end(); it++) {
        const auto w_above = *it;

        // ignore if windows don't overlap
        if (!w->frameGeometry().intersects(w_above->frameGeometry())) {
            continue;
        }

        if (const auto bbdx_w_above = findWindow(w_above); bbdx_w_above != nullptr) {
            if (bbdx_w_above->isBlurred()) {
                w_above->addRepaintFull();
            }
        }
    }
}
