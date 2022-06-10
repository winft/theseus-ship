########### next target ###############
include(ECMSetupVersion)

ecm_setup_version(${CMAKE_PROJECT_VERSION}
                  VARIABLE_PREFIX KWINEFFECTS
                  VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kwineffects_version.h"
                  PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/KWinEffectsConfigVersion.cmake"
                  SOVERSION 13
)

### xrenderutils lib ###
set(kwin_XRENDERUTILS_SRCS
    kwinxrender/utils.cpp
)

add_library(kwinxrenderutils SHARED ${kwin_XRENDERUTILS_SRCS})
generate_export_header(kwinxrenderutils EXPORT_FILE_NAME kwinxrender/export.h)
target_link_libraries(kwinxrenderutils
    PUBLIC
        Qt::Core
        Qt::Gui

        XCB::IMAGE
        XCB::RENDER
        XCB::XCB
        XCB::XFIXES
)

set_target_properties(kwinxrenderutils PROPERTIES
    VERSION ${KWINEFFECTS_VERSION}
    SOVERSION ${KWINEFFECTS_SOVERSION}
)
set_target_properties(kwinxrenderutils PROPERTIES OUTPUT_NAME ${KWIN_NAME}xrenderutils)

install(TARGETS kwinxrenderutils EXPORT KWinEffectsTargets ${INSTALL_TARGETS_DEFAULT_ARGS})

###  effects lib  ###
set(kwin_EFFECTSLIB_SRCS
    kwineffects/anidata.cpp
    kwineffects/animation_effect.cpp
    kwineffects/offscreen_effect.cpp
    kwineffects/effect.cpp
    kwineffects/effect_frame.cpp
    kwineffects/effect_plugin_factory.cpp
    kwineffects/effect_quick_view.cpp
    kwineffects/effect_screen.cpp
    kwineffects/effect_window.cpp
    kwineffects/effects_handler.cpp
    kwineffects/motions.cpp
    kwineffects/paint_clipper.cpp
    kwineffects/paint_data.cpp
    kwineffects/shared_qml_engine.cpp
    kwineffects/time_line.cpp
    kwineffects/window_quad.cpp
)

add_library(kwineffects SHARED ${kwin_EFFECTSLIB_SRCS})
generate_export_header(kwineffects EXPORT_FILE_NAME kwineffects/export.h)
target_link_libraries(kwineffects
    PUBLIC
        KF5::ConfigCore
        KF5::CoreAddons
        KF5::WindowSystem
        XCB::XCB
        WraplandServer
    PRIVATE
        Qt::Quick
        KF5::I18n
        kwinglutils
)
if (KWIN_HAVE_XRENDER_COMPOSITING)
    target_link_libraries(kwineffects PRIVATE kwinxrenderutils XCB::XFIXES)
endif()
set_target_properties(kwineffects PROPERTIES
    VERSION ${KWINEFFECTS_VERSION}
    SOVERSION ${KWINEFFECTS_SOVERSION}
)
set_target_properties(kwineffects PROPERTIES OUTPUT_NAME ${KWIN_NAME}effects)

install(TARGETS kwineffects EXPORT KWinEffectsTargets ${INSTALL_TARGETS_DEFAULT_ARGS})

# kwingl(es)utils library
set(kwin_GLUTILSLIB_SRCS
    kwingl/platform.cpp
    kwingl/texture.cpp
    kwingl/utils.cpp
    kwingl/utils_funcs.cpp
)

add_library(kwinglutils SHARED ${kwin_GLUTILSLIB_SRCS})
generate_export_header(kwinglutils BASE_NAME kwinglutils EXPORT_FILE_NAME kwingl/export.h)
target_link_libraries(kwinglutils PUBLIC XCB::XCB KF5::CoreAddons KF5::ConfigCore KF5::WindowSystem epoxy::epoxy)
set_target_properties(kwinglutils PROPERTIES
    VERSION ${KWINEFFECTS_VERSION}
    SOVERSION ${KWINEFFECTS_SOVERSION}
    OUTPUT_NAME ${KWIN_NAME}glutils
)

install(TARGETS kwinglutils EXPORT KWinEffectsTargets ${INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/kwinconfig.h
    kwineffects/legacy/kwinanimationeffect.h
    kwineffects/legacy/kwinoffscreeneffect.h
    kwineffects/legacy/kwineffectquickview.h
    kwineffects/legacy/kwineffects.h
    kwineffects/legacy/kwineffects_export.h
    kwingl/legacy/kwinglplatform.h
    kwingl/legacy/kwingltexture.h
    kwingl/legacy/kwinglutils.h
    kwingl/legacy/kwinglutils_export.h
    kwingl/legacy/kwinglutils_funcs.h
    kwinxrender/legacy/kwinxrenderutils.h
    kwinxrender/legacy/kwinxrenderutils_export.h
    kwinglobals.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR} COMPONENT Devel)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/kwineffects/export.h
    kwineffects/animation_effect.h
    kwineffects/offscreen_effect.h
    kwineffects/effect.h
    kwineffects/effect_frame.h
    kwineffects/effect_integration.h
    kwineffects/effect_plugin_factory.h
    kwineffects/effect_quick_view.h
    kwineffects/effect_screen.h
    kwineffects/effect_window.h
    kwineffects/effects_handler.h
    kwineffects/motions.h
    kwineffects/paint_clipper.h
    kwineffects/paint_data.h
    kwineffects/time_line.h
    kwineffects/types.h
    kwineffects/window_quad.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwineffects COMPONENT Devel)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/kwingl/export.h
    kwingl/platform.h
    kwingl/texture.h
    kwingl/utils.h
    kwingl/utils_funcs.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwingl COMPONENT Devel)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/kwinxrender/export.h
    kwinxrender/utils.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinxrender COMPONENT Devel)

set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KWinEffects")


install(FILES  "${CMAKE_CURRENT_BINARY_DIR}/KWinEffectsConfig.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/KWinEffectsConfigVersion.cmake"
        DESTINATION "${CMAKECONFIG_INSTALL_DIR}"
        COMPONENT Devel )

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/KWinEffectsConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/KWinEffectsConfig.cmake"
  INSTALL_DESTINATION  ${CMAKECONFIG_INSTALL_DIR}
)

install(EXPORT KWinEffectsTargets DESTINATION "${CMAKECONFIG_INSTALL_DIR}" FILE KWinEffectsTargets.cmake NAMESPACE KWinEffects:: )
