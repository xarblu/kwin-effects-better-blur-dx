#pragma once

#include "blurconfig.h"
#include "window.hpp"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QObject>
#include <QList>
#include <QRegularExpression>
#include <QMap>
#include <QString>

#include <optional>
#include <utility>

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
    QMap<const KWin::EffectWindow *, BBDX::Window *> m_windows{};

    // window classes
    QList<QString> m_windowClassesFixed{};
    QList<QRegularExpression> m_windowClassesRegex{};
    WindowClassMatchMode m_windowClassMatchMode{WindowClassMatchMode::Whitelist};

    // window/region types
    bool m_blurDecorations{false};
    bool m_blurDocks{false};
    bool m_blurMenus{false};

    // match helpers
    bool ignoreWindow(const KWin::EffectWindow *w) const;
    bool matchesWindowClassFixed(const KWin::EffectWindow *w) const;
    bool matchesWindowClassRegex(const KWin::EffectWindow *w) const;

public Q_SLOT:
    void slotWindowAdded(const KWin::EffectWindow *w);
    void slotWindowDeleted(const KWin::EffectWindow *w);

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
     * setters
     */
    void setWindowClassesFixed(QList<QString> windowClasses) {
        m_windowClassesFixed = std::move(windowClasses);
    }
    void setWindowClassesRegex(QList<QRegularExpression> windowClasses) {
        m_windowClassesRegex = std::move(windowClasses);
    }
    void setWindowClassMatchMode(WindowClassMatchMode mode) {
        m_windowClassMatchMode = mode;
    }
    void setBlurDecorations(bool toggle) {
        m_blurDecorations = toggle;
    }
    void setBlurDocks(bool toggle) {
        m_blurDocks = toggle;
    }
    void setBlurMenus(bool toggle) {
        m_blurMenus = toggle;
    }

    /**
     * getters
     */
    bool blurDecorations() const { return m_blurDecorations; }

    /**
     * Find a managed window, nullptr if not found
     */
    BBDX::Window* findWindow(const KWin::EffectWindow *w) const;

    /**
     * Match an EffectWindow instance
     */
    bool shouldForceBlur(const KWin::EffectWindow *w) const;
};

} // namespace KWin
