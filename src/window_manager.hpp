#pragma once

#include "kwin_version.hpp"
#include "window.hpp"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#if KWIN_VERSION < KWIN_VERSION_CODE(6, 5, 80) || defined(BETTERBLUR_X11)
#  include "kwin_compat_6_6.hpp"
#else
#  include <core/output.h>
#  include <core/region.h>
#endif

#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QString>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace KWin {
    class BorderRadius;
}

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
    // managed windows
    std::unordered_map<const KWin::EffectWindow *, std::unique_ptr<BBDX::Window>> m_windows{};

    // docks seperate for maximized calculation
    std::unordered_set<const KWin::EffectWindow *> m_docks{};

    // window classes
    QList<QString> m_windowClassesFixed{};
    QList<QRegularExpression> m_windowClassesRegex{};
    WindowClassMatchMode m_windowClassMatchMode{WindowClassMatchMode::Whitelist};

    // window/region types
    bool m_blurDecorations{false};
    bool m_blurDocks{false};
    bool m_blurMenus{false};

    // user configured border radius
    qreal m_userBorderRadius{0.0};

    // match helpers
    bool ignoreWindow(const KWin::EffectWindow *w) const;
    bool matchesWindowClassFixed(const KWin::EffectWindow *w) const;
    bool matchesWindowClassRegex(const KWin::EffectWindow *w) const;

    /**
     * Find a managed window, nullptr if not found
     */
    BBDX::Window* findWindow(const KWin::EffectWindow *w) const;

public Q_SLOT:
    void slotWindowAdded(KWin::EffectWindow *w);
    void slotWindowDeleted(KWin::EffectWindow *w);

signals:
    void windowWantsBlurRegionUpdate(KWin::EffectWindow *w) const;

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

    /**
     * Refresh maximized state of a window
     */
    void refreshMaximizedState(const KWin::EffectWindow *w);

    /**
     * getters
     */
    bool blurDecorations() const { return m_blurDecorations; }
    qreal userBorderRadius() const { return m_userBorderRadius; }

    /**
     * Match an EffectWindow instance
     */
    bool shouldForceBlur(const KWin::EffectWindow *w) const;

    /**
     * emits the wantsBlurRegionUpdate signal
     */
    void triggerBlurRegionUpdate(KWin::EffectWindow *w) const;

    /**
     * Set the "window is transformed" flag on a window
     * (scaled, translated or PAINT_WINDOW_TRANSFORMED set)
     */
    void setWindowIsTransformed(const KWin::EffectWindow *w, bool toggle) const;

    /**
     * Check if this window should currently be blurred
     * even when PAINT_WINDOW_TRANSFORMED is set
     */
    bool windowShouldBlurWhileTransformed(const KWin::EffectWindow *w) const;

    /**
     * Get the final blur region for a window, set in content/frame.
     *
     * Forwarded to BBDX::Window::getFinalBlurRegion() if w is managed
     * else does nothing.
     */
    void getFinalBlurRegion(const KWin::EffectWindow *w, std::optional<KWin::Region> &content, std::optional<KWin::Region> &frame) const;

    /**
     * Get effective border radius for requested window,
     * or empty if unmanaged
     */
    KWin::BorderRadius getEffectiveBorderRadius(const KWin::EffectWindow *w) const;

    /**
     * Get effective blur opacity for requested window
     */
    qreal getEffectiveBlurOpacity(const KWin::EffectWindow *w, KWin::WindowPaintData &data) const;
};

} // namespace KWin
