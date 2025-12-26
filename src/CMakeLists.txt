add_subdirectory(kcm)

set(better_blur_dx_SOURCES
    blur.cpp
    blur_extensions.cpp
    blur.qrc
    main.cpp
    settings.cpp
    window_matcher.cpp
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
    target_compile_definitions(better_blur_dx PRIVATE BETTERBLUR_X11)
    install(TARGETS better_blur_dx DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin-x11/effects/plugins)
else()
    add_library(better_blur_dx MODULE ${better_blur_dx_SOURCES})
    target_link_libraries(better_blur_dx PRIVATE
        KDecoration3::KDecoration
        KF6::ConfigGui
        KWin::kwin
    )
    install(TARGETS better_blur_dx DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)
endif()
