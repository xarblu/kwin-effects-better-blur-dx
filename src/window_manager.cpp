#include "window_manager.hpp"

#include "blurconfig.h"
#include "kwin_version.hpp"
#include "utils.h"
#include "window.hpp"

#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <qtpreprocessorsupport.h>
#include <scene/borderradius.h>
#include <window.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80)
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
    m_userBlurCulling = config->blurCulling();

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

bool BBDX::WindowManager::windowIsBlurred(const KWin::EffectWindow *w) const {
    const auto window = findWindow(w);

    // assume false for unmanaged windows
    if (!window)
        return false;

    return window->isBlurred();
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

void BBDX::WindowManager::applyBlurRegionCulling(const KWin::EffectWindow *w, const KWin::Region &blurRegion, KWin::Region &blurShape, const KWin::WindowPaintData &data) const {
    if (!m_userBlurCulling) {
        return;
    }

    KWin::Region mask{};

    for (const auto &[kWindow, bbdxWindow] : m_windows) {
        // only windows higher in the stack should cull
        if (kWindow->window()->stackingOrder() <= w->window()->stackingOrder()) {
            continue;
        }

        // filter some incompatible window types
        if (kWindow->isSpecialWindow()
            || kWindow->window()->isInternal()
            || !kWindow->isVisible()
            || bbdxWindow->isMenu()
            || bbdxWindow->isTransformed()) {
            continue;
        }

        KWin::Region windowMask{KWin::RectF{kWindow->frameGeometry().translated(-w->pos().toPoint())}.rounded()};

        // clip corners with border radius because we may still need blur underneath
        // TODO: figure out good minimum (or grab from user config)
        constexpr qreal minRadius{5.0};
        KWin::BorderRadius borderRadius{bbdxWindow->getEffectiveBorderRadius()};
        borderRadius = KWin::BorderRadius{
            std::max(minRadius, borderRadius.topLeft()),
            std::max(minRadius, borderRadius.topRight()),
            std::max(minRadius, borderRadius.bottomLeft()),
            std::max(minRadius, borderRadius.bottomRight()),
        };

        mask += borderRadius.clip(windowMask, windowMask.boundingRect());
    }

    // Compute the effective blur shape, now with culling. Note that if the window is transformed, so will be the blur shape.
    // Basically a copy of the version in Effect::blur().
    // TODO: This is kinda stupid but backgroundRect in Effect::blur() needs
    //       the non-culled version of the blurShape.
    blurShape = blurRegion.subtracted(mask).translated(w->pos().toPoint());
    if (data.xScale() != 1 || data.yScale() != 1) {
        QPoint pt = blurShape.boundingRect().topLeft();
        KWin::Region scaledShape;
        for (const KWin::Rect &r : blurShape.rects()) {
            const QPointF topLeft(pt.x() + (r.x() - pt.x()) * data.xScale() + data.xTranslation(),
                                  pt.y() + (r.y() - pt.y()) * data.yScale() + data.yTranslation());
            const QPoint bottomRight(std::floor(topLeft.x() + r.width() * data.xScale()) - 1,
                                     std::floor(topLeft.y() + r.height() * data.yScale()) - 1);
            scaledShape += QRect(QPoint(std::floor(topLeft.x()), std::floor(topLeft.y())), bottomRight);
        }
        blurShape = scaledShape;
    } else if (data.xTranslation() || data.yTranslation()) {
        blurShape.translate(std::round(data.xTranslation()), std::round(data.yTranslation()));
    }
}
