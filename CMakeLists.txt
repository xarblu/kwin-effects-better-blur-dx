cmake_minimum_required(VERSION 3.16.0)

project(kwin_better_blur_dx)
set(PROJECT_VERSION "2.2.0")
set(PROJECT_VERSION_MAJOR 0)

set(KF_MIN_VERSION "5.240.0")
set(QT_MIN_VERSION "6.6.0")
set(QT_MAJOR_VERSION 6)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CPACK_PACKAGE_NAME "kwin-better-blur-dx")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_FILE_NAME "kwin-better-blur-dx")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fork of kwin-better-blur, which itself is a fork of the default KDE Blur that includes useful additions, with continued development")
include(CPack)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DQT_NO_DEBUG_OUTPUT")

# User Build Options
option(BETTERBLUR_X11 "Build Better Blur DX for KWin X11 instead of KWin Wayland" OFF)

# Dependencies
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core
    DBus
    Gui
    Network
    OpenGL
    Qml
    Quick
    Widgets
)

include_directories(${Qt6Widgets_INCLUDE_DIRS} ${Qt6Network_INCLUDE_DIRS} ${Qt6OpenGL_INCLUDE_DIRS} ${Qt6Xml_INCLUDE_DIRS})
add_definitions(${Qt6Widgets_DEFINITIONS})

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    ColorScheme
    Config
    ConfigWidgets
    CoreAddons
    I18n
    KCMUtils
    WidgetsAddons
    WindowSystem
)

find_package(KDecoration3 REQUIRED)

find_package(epoxy REQUIRED)

find_package(X11 REQUIRED)
find_package(XCB REQUIRED COMPONENTS XCB)

if(BETTERBLUR_X11)
    find_package(KWinX11 REQUIRED COMPONENTS kwineffects)
    find_package(KWinX11DBusInterface CONFIG REQUIRED)
    set(KWin_VERSION ${KWinX11_VERSION})
    set(KWin_VERSION_MAJOR ${KWinX11_VERSION_MAJOR})
    set(KWin_VERSION_MINOR ${KWinX11_VERSION_MINOR})
    set(KWin_VERSION_PATCH ${KWinX11_VERSION_PATCH})
else()
    find_package(KWin REQUIRED COMPONENTS kwineffects)
    find_package(KWinDBusInterface CONFIG REQUIRED)
endif()

if(${KWin_VERSION} VERSION_LESS 6.4)
    message(FATAL_ERROR "Better Blur DX does not support your Plasma version (${KWin_VERSION}). See the README for more information.")
endif()

add_subdirectory(src)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
