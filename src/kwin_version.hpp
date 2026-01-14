#pragma once

#ifndef KWIN_VERSION_MAJOR
# error KWIN_VERSION_MAJOR not defined
#endif // !KWIN_VERSION_MAJOR

#ifndef KWIN_VERSION_MINOR
# error KWIN_VERSION_MINOR not defined
#endif // !KWIN_VERSION_MINOR

#ifndef KWIN_VERSION_PATCH
# error KWIN_VERSION_PATCH not defined
#endif // !KWIN_VERSION_PATCH

#define KWIN_VERSION_CODE(X, Y, Z) \
    (X << 16) + \
    (Y << 8) + \
    (Z << 0)

#define KWIN_VERSION KWIN_VERSION_CODE(KWIN_VERSION_MAJOR, KWIN_VERSION_MINOR, KWIN_VERSION_PATCH)
