add_subdirectory(kcm)

function(replace_shader_include input output)
    file(READ shaders/roundedcorners.glsl ROUNDEDCORNERS_SHADER)
    file(READ "${input}" SHADER)
    string(REPLACE "#include \"roundedcorners.glsl\"" "${ROUNDEDCORNERS_SHADER}" SHADER "${SHADER}")
    file(WRITE "${output}" "${SHADER}")
endfunction()

replace_shader_include(shaders/texture.glsl shaders/texture.frag)
replace_shader_include(shaders/texture_core.glsl shaders/texture_core.frag)
replace_shader_include(shaders/upsample.glsl shaders/upsample.frag)
replace_shader_include(shaders/upsample_core.glsl shaders/upsample_core.frag)
replace_shader_include(shaders/refraction.glsl shaders/refraction.frag)
replace_shader_include(shaders/refraction_core.glsl shaders/refraction_core.frag)

set(better_blur_dx_SOURCES
    blur.cpp
    blur.qrc
    main.cpp
    settings.cpp
)

kconfig_add_kcfg_files(better_blur_dx_SOURCES
    blurconfig.kcfgc
)

if(BETTERBLUR_WAYLAND)
    add_library(better_blur_dx MODULE ${better_blur_dx_SOURCES})
    target_link_libraries(better_blur_dx PRIVATE
        KDecoration3::KDecoration
        KF6::ConfigGui
        KWin::kwin
    )
    install(TARGETS better_blur_dx DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)
endif()
if(BETTERBLUR_X11)
    add_library(better_blur_dx_x11 MODULE ${better_blur_dx_SOURCES})
    target_link_libraries(better_blur_dx_x11 PRIVATE
        KDecoration3::KDecoration
        KF6::ConfigGui
        KWinX11::kwin
    )
    target_compile_definitions(better_blur_dx_x11 PRIVATE BETTERBLUR_X11)
    install(TARGETS better_blur_dx_x11 DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin-x11/effects/plugins)
endif()
