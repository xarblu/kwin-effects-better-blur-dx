add_subdirectory(kcm)

set(better_blur_dx_SOURCES
    blur.cpp
    blur.qrc
    blur_extensions.cpp
    main.cpp
    refraction_pass.cpp
    rounded_corners_pass.cpp
    settings.cpp
    window.cpp
    window_manager.cpp
)

kconfig_add_kcfg_files(better_blur_dx_SOURCES
    blurconfig.kcfgc
)

if(BETTERBLUR_X11)
    add_library(better_blur_dx MODULE ${better_blur_dx_SOURCES})
    target_link_libraries(better_blur_dx PRIVATE
        KDecoration3::KDecoration
        KF6::ConfigGui
        KWinX11::kwin
    )
    target_compile_definitions(better_blur_dx PRIVATE
                               KWIN_VERSION_MAJOR=${KWin_VERSION_MAJOR}
                               KWIN_VERSION_MINOR=${KWin_VERSION_MINOR}
                               KWIN_VERSION_PATCH=${KWin_VERSION_PATCH}
                               BETTERBLUR_X11)
    install(TARGETS better_blur_dx DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin-x11/effects/plugins)
else()
    add_library(better_blur_dx MODULE ${better_blur_dx_SOURCES})
    target_link_libraries(better_blur_dx PRIVATE
        KDecoration3::KDecoration
        KF6::ConfigGui
        KWin::kwin
    )
    target_compile_definitions(better_blur_dx PRIVATE
                               KWIN_VERSION_MAJOR=${KWin_VERSION_MAJOR}
                               KWIN_VERSION_MINOR=${KWin_VERSION_MINOR}
                               KWIN_VERSION_PATCH=${KWin_VERSION_PATCH})
    install(TARGETS better_blur_dx DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)
endif()

target_compile_options(better_blur_dx PRIVATE -Wall -Wextra -Wunused -Wpedantic)
