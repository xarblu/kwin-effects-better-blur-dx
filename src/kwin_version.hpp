#pragma once

#if !defined(KWIN_VERSION_MAJOR)
# error KWIN_VERSION_MAJOR not defined
#endif // !KWIN_VERSION_MAJOR

#if !defined(KWIN_VERSION_MINOR)
# error KWIN_VERSION_MINOR not defined
#endif // !KWIN_VERSION_MINOR

#if !defined(KWIN_VERSION_PATCH)
# error KWIN_VERSION_PATCH not defined
#endif // !KWIN_VERSION_PATCH

#define KWIN_VERSION_CODE(X, Y, Z) \
    (X << 16) + \
    (Y << 8) + \
    (Z << 0)

// KWin-X11 is essentially locked at its 6.5 API
// and we don't support older versions.
// So for simplicity assume that's its version.
#if defined(BETTERBLUR_X11)
# define KWIN_VERSION KWIN_VERSION_CODE(6, 5, 0)
#else
# define KWIN_VERSION KWIN_VERSION_CODE(KWIN_VERSION_MAJOR, KWIN_VERSION_MINOR, KWIN_VERSION_PATCH)
#endif
