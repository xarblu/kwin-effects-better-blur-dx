#pragma once

#include "kwin_version.hpp"
#include "window.hpp"

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

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
#include <QObject>
#include <QRegularExpression>
#include <QString>

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace KWin {
    class BorderRadius;
}

namespace BBDX {
class BlurEffect;

class WindowManager : public QObject {
    Q_OBJECT

public:
    enum class WindowClassMatchMode {
        Whitelist,
        Blacklist,
    };

private:
    // pointer to the owning BlurEffect instance
    BBDX::BlurEffect *m_effect;

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
    bool matchesWindowClassFixed(const KWin::EffectWindow *w) const;
    bool matchesWindowClassRegex(const KWin::EffectWindow *w) const;

    /**
     * Find a managed window, nullptr if not found
     */
    BBDX::Window* findWindow(const KWin::EffectWindow *w) const;

    /**
     * Collect BBDX Windows sorted by stackingOrder
     */
    std::vector<BBDX::Window *> windowsByStackingOrder() const;

public Q_SLOT:
    void slotWindowAdded(KWin::EffectWindow *w);
    void slotWindowDeleted(KWin::EffectWindow *w);
    void slotStackingOrderChanged();

public:
    explicit WindowManager(BBDX::BlurEffect *effect);

    /**
     * reconfigure from BlurConfig
     */
    void reconfigure();

    /**
     * Refresh maximized state of a window / of all windows
     */
    void refreshMaximizedState(BBDX::Window *w) const;
    void refreshMaximizedStateAll() const;

    /**
     * Refresh window coverage info of a window / of all windows
     */
    void refreshWindowCoverage(BBDX::Window *bbdxWindow) const;
    void refreshWindowCoverageAll() const;

    /**
     * getters
     */
    bool blurDecorations() const { return m_blurDecorations; }
    bool blurDocks() const { return m_blurDocks; }
    bool blurMenus() const { return m_blurMenus; }
    qreal userBorderRadius() const { return m_userBorderRadius; }

    /**
     * Match an EffectWindow instance in the black/white list
     * XXX: should this also be moved to BBDX::Window?
     */
    bool shouldForceBlurWindowClass(const KWin::EffectWindow *w) const;

    /**
     * emits the wantsBlurRegionUpdate signal
     */
    void triggerBlurRegionUpdate(KWin::EffectWindow *w) const;

    /**
     * emits the windowInvalidatedBlurCache signal
     */
    void invalidateBlurCache(KWin::EffectWindow *w, QStringView reason) const;

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
     * Check if this window is blurred in any way (requested or forced)
     */
    bool windowIsBlurred(const KWin::EffectWindow *w) const;

    /**
     * Get the final blur region for a window, set in content/frame.
     *
     * Forwarded to BBDX::Window::getFinalBlurRegion() if w is managed
     * else does nothing.
     */
    void getFinalBlurRegion(const KWin::EffectWindow *w, std::optional<KWin::RegionF> &content, std::optional<KWin::RegionF> &frame) const;

    /**
     * Get effective border radius for requested window,
     * or empty if unmanaged
     */
    KWin::BorderRadius getEffectiveBorderRadius(const KWin::EffectWindow *w) const;

    /**
     * Get effective blur opacity for requested window
     */
    qreal getEffectiveBlurOpacity(const KWin::EffectWindow *w, KWin::WindowPaintData &data) const;

    /**
     * Check if the provided window's blur region is fully covered by
     * the frame geometry of other windows
     */
    bool windowIsBlurFullyCovered(KWin::EffectWindow *w) const;

    /**
     * Add a full repaint to all blurred windows
     */
    void repaintAllBlurredWindows() const;
};

} // namespace KWin
