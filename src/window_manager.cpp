#include "window_manager.hpp"

#include "blur.h"
#include "blurconfig.h"
#include "kwin_version.hpp"
#include "utils.h"
#include "window.hpp"

#include <core/outputlayer.h>
#include <core/pixelgrid.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <qloggingcategory.h>
#include <scene/borderradius.h>
#include <scene/windowitem.h>
#include <window.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
#  include "kwin_compat_6_5.hpp"
#else
#  include <core/output.h>
#  include <core/region.h>
#endif
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
#  include "kwin_compat_6_6.hpp"
#endif

#include <QList>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QString>

#include <memory>
#include <utility>
#include <vector>

Q_LOGGING_CATEGORY(WINDOW_MANAGER, "kwin_effect_better_blur_dx.window_manager", QtInfoMsg)

BBDX::WindowManager::WindowManager(BBDX::BlurEffect *effect) {
    m_effect = effect;

    // add existing windows
    for (const auto &window : KWin::effects->stackingOrder()) {
        slotWindowAdded(window);
    }

    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &WindowManager::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, &WindowManager::slotWindowDeleted);
    connect(KWin::effects, &KWin::EffectsHandler::stackingOrderChanged, this, &WindowManager::slotStackingOrderChanged);
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

void BBDX::WindowManager::slotStackingOrderChanged() {
    refreshWindowCoverageAll();
    repaintAllBlurredWindows();
}

BBDX::Window* BBDX::WindowManager::findWindow(const KWin::EffectWindow *w) const {
    if (const auto it = m_windows.find(w); it != m_windows.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<BBDX::Window *> BBDX::WindowManager::windowsByStackingOrder() const {
    std::vector<BBDX::Window *> windows{};

    for (const auto &[kWindow, bbdxWindow] : m_windows) {

        // insert sorted by stackingOrder
        for (auto it = windows.begin(); ; it++) {
            if (it == windows.end()) {
                windows.insert(it, bbdxWindow.get());
                break;
            }

            if ((*it)->effectwindow()->window()->stackingOrder() > kWindow->window()->stackingOrder()) {
                windows.insert(it, bbdxWindow.get());
                break;
            }
        }
    }

    return windows;
}

void BBDX::WindowManager::reconfigure() {
    auto config = BBDX::BlurConfig::self();

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
    /**
     * TODO: convert to RegionF
     */

    const KWin::EffectWindow *w = window->effectwindow();

    const KWin::LogicalOutput* screen = w->screen();

    KWin::Region effectiveScreenRegion = KWin::Region(screen->geometry());

    for (const auto &dock : m_docks) {
        if (dock->screen() != screen)  {
            continue;
        }

        // we need to "expand" the docks to their entire screen edge
        // or the boundingRect is wrong
        KWin::Rect dock_rect{dock->frameGeometry().toRect()};

        if (dock_rect.width() > dock_rect.height()) {
            // horizontal
            dock_rect.setX(dock->screen()->geometry().x());
            dock_rect.setWidth(dock->screen()->geometry().width());
        } else {
            // vertical
            dock_rect.setY(dock->screen()->geometry().y());
            dock_rect.setHeight(dock->screen()->geometry().height());
        }

        effectiveScreenRegion -= KWin::Region(dock_rect);
    }

    const KWin::Rect effectiveScreenRect{effectiveScreenRegion.boundingRect()};
    const KWin::Rect windowRect{KWin::Rect(w->frameGeometry().toRect())};

    bool maximizedHorizontal{
        windowRect.left() <= effectiveScreenRect.left()
        && windowRect.right() >= effectiveScreenRect.right()
    };

    bool maximizedVertical{
        windowRect.top() <= effectiveScreenRect.top()
        && windowRect.bottom() >= effectiveScreenRect.bottom()
    };

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

void BBDX::WindowManager::refreshWindowCoverage(BBDX::Window *bbdxWindow) const {
    const auto w = bbdxWindow->effectwindow();

    KWin::RegionF blurRegion{m_effect->blurRegion(w)};
#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
    blurRegion.translate(w->pos().toPoint());
#else
    blurRegion.translate(w->pos());
#endif

    for (const auto &[kWindow, bbdxWindow] : m_windows) {
        // ignore these
        if (kWindow == w
            ||kWindow->isDesktop()
            || !kWindow->isVisible()) {
            continue;
        }

        if (kWindow->window()->stackingOrder() <= w->window()->stackingOrder()) {
            continue;
        }

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 6, 90)
        blurRegion -= kWindow->frameGeometry().toRect();
#else
        blurRegion -= kWindow->frameGeometry();
#endif

        if (blurRegion.isEmpty()) {
            break;
        }
    }

    bbdxWindow->setIsBlurFullyCovered(blurRegion.isEmpty());
}

void BBDX::WindowManager::refreshWindowCoverageAll() const {
    for (const auto &[w, window] : m_windows) {
        refreshWindowCoverage(window.get());
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
    m_effect->updateBlurRegion(w);
}

void BBDX::WindowManager::invalidateBlurCache(KWin::EffectWindow *w, QStringView reason) const {
    if (auto it = m_effect->m_windows.find(w); it != m_effect->m_windows.end()) {
        BBDX::BlurEffectData &blurInfo = it->second;
        for (auto &[_, renderInfo] : blurInfo.render) {
            renderInfo.cache.invalidate(reason);
        }
    }
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

bool BBDX::WindowManager::windowIsBlurred(const KWin::EffectWindow *w) const {
    const auto window = findWindow(w);

    // assume false for unmanaged windows
    if (!window)
        return false;

    return window->isBlurred();
}

void BBDX::WindowManager::getFinalBlurRegion(const KWin::EffectWindow *w, std::optional<KWin::RegionF> &content, std::optional<KWin::RegionF> &frame) const {
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

bool BBDX::WindowManager::windowIsBlurFullyCovered(KWin::EffectWindow *w) const {
    const auto window = findWindow(w);

    // for unmanaged windows assume false so they
    // don't get throttled accidentally
    if (!window)
        return false;

    return window->isBlurFullyCovered();
}

void BBDX::WindowManager::repaintAllBlurredWindows() const {
    for (const auto &[kWindow, bbdxWindow] : m_windows) {
        if (!bbdxWindow->isBlurred()) {
            continue;
        }

        const_cast<KWin::EffectWindow *>(kWindow)->addRepaintFull();
    }
}
