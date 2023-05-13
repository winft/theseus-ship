# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(kwin_effects_dbus_xml ${CMAKE_CURRENT_SOURCE_DIR}/render/dbus/org.kde.kwin.Effects.xml)
qt6_add_dbus_interface(effects_interface_SRCS ${kwin_effects_dbus_xml} kwineffects_interface)
add_library(KWinEffectsInterface STATIC ${effects_interface_SRCS})
target_link_libraries(KWinEffectsInterface Qt::DBus)
set_target_properties(KWinEffectsInterface PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

add_subdirectory(base)
add_subdirectory(effect)
add_subdirectory(win)
add_subdirectory(input)
add_subdirectory(render)
add_subdirectory(data)
add_subdirectory(script)
add_subdirectory(xwl)

### Common library
set(kwin_SRCS
    desktop/kde/dbus/kwin.cpp
    desktop/screen_locker_watcher.cpp
    debug/console/console.cpp
    debug/perf/ftrace.cpp
    render/x11/compositor_selection_owner.cpp
    script/dbus_call.cpp
    script/desktop_background_item.cpp
    script/js_engine_global_methods_wrapper.cpp
    script/screen_edge_handler.cpp
    script/effect.cpp
    script/options.cpp
    script/output.cpp
    script/platform.cpp
    script/script.cpp
    script/script_timer.cpp
    script/scripting_logging.cpp
    script/shortcut_handler.cpp
    script/singleton_interface.cpp
    script/utils.cpp
    script/virtual_desktop_model.cpp
    script/window.cpp
    script/window_model.cpp
    script/window_thumbnail_item.cpp
    script/space.cpp
)

qt6_add_dbus_adaptor(kwin_SRCS
  script/org.kde.kwin.Script.xml
  script/script.h
  KWin::scripting::abstract_script
)

qt6_add_dbus_adaptor(kwin_SRCS
  desktop/kde/dbus/org.kde.KWin.xml
  desktop/kde/dbus/kwin.h
  KWin::desktop::kde::kwin
)

qt6_add_dbus_interface(kwin_SRCS ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/kf6_org.freedesktop.ScreenSaver.xml screenlocker_interface)
qt6_add_dbus_interface(kwin_SRCS ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/org.kde.screensaver.xml kscreenlocker_interface)

ki18n_wrap_ui(kwin_SRCS
    debug/console/debug_console.ui
)

set(kwin_KDE_LIBS
  KF6::ConfigCore
  KF6::Service
)

set(kwin_XCB_LIBS
    XCB::ICCCM
    XCB::RENDER
    XCB::SHM
    XCB::XFIXES
)

set(kwin_INPUT_LIBS
  XKB::XKB
)

set(kwinLibs
    base-x11
    input
    render
    win
    ${kwin_KDE_LIBS}
    ${kwin_XCB_LIBS}
    ${kwin_INPUT_LIBS}

    # For render code
    kwinxrenderutils
)

add_library(kwin SHARED ${kwin_SRCS})

set_target_properties(kwin PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
)

target_precompile_headers(kwin PRIVATE
    cmake/precompiled/qt.h
    cmake/precompiled/std.h
    cmake/precompiled/xcb.h
)
target_link_libraries(kwin ${kwinLibs} epoxy::epoxy)

if(HAVE_PERF)
    target_sources(kwin PRIVATE debug/perf/ftrace_impl.cpp)
endif()

generate_export_header(kwin EXPORT_FILE_NAME kwin_export.h)

### X11 library
set(kwin_X11_SRCS
   base/backend/x11/platform.cpp
   render/backend/x11/non_composited_outline.cpp
)

if (HAVE_EPOXY_GLX)
    set(kwin_X11_SRCS
        ${kwin_X11_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/render/backend/x11/glx_context_attribute_builder.cpp
    )
endif()

add_library(kwin_x11_lib STATIC
  ${kwin_X11_SRCS}
)

target_link_libraries(kwin_x11_lib
PUBLIC
  kwin
  input-x11
  KF6::Crash
PRIVATE
  Qt::GuiPrivate
  X11::Xi
)

if (HAVE_DL_LIBRARY)
    target_link_libraries(kwin_x11_lib PRIVATE ${DL_LIBRARY})
endif()

### Wayland library
set(kwin_WAYLAND_SRCS
   base/backend/wlroots/drm_lease.cpp
   base/backend/wlroots/non_desktop_output.cpp
   base/backend/wlroots/output.cpp
   base/backend/wlroots/platform.cpp
   base/seat/backend/wlroots/session.cpp
   debug/console/wayland/input_device_model.cpp
   debug/console/wayland/wayland_console.cpp
   input/backend/wlroots/platform.cpp
   input/backend/wlroots/control/headless/keyboard.cpp
   input/backend/wlroots/control/keyboard.cpp
   input/backend/wlroots/control/pointer.cpp
   input/backend/wlroots/control/switch.cpp
   input/backend/wlroots/control/touch.cpp
)

if (KWIN_BUILD_TABBOX)
  set(kwin_WAYLAND_SRCS
    ${kwin_WAYLAND_SRCS}
  )
endif()

add_library(kwin_wayland_lib SHARED
  ${kwin_WAYLAND_SRCS}
)

target_link_libraries(kwin_wayland_lib
PUBLIC
  kwin
  input-wl
  xwl
  WraplandClient
  Wayland::Server
  wlroots::wlroots
  Pixman::Pixman
  Libinput::Libinput
  Threads::Threads
)

kcoreaddons_target_static_plugins(
  kwin_wayland_lib "kwin/effects/plugins"
  LINK_OPTION "PRIVATE"
)

set_target_properties(kwin_wayland_lib PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   OUTPUT_NAME "kwin_wayland"
)

target_compile_definitions(kwin_wayland_lib PUBLIC WLR_USE_UNSTABLE)


### Installs
install(TARGETS kwin ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS kwin_wayland_lib ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)

install(FILES desktop/kde/kwin.notifyrc
  DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
  RENAME ${KWIN_NAME}.notifyrc
)
install(
  FILES
    desktop/kde/dbus/org.kde.KWin.xml

    # Still needs to be installed as plasma-workspace expects it at compile time.
    data/org.kde.kwin.VirtualKeyboard.xml
  DESTINATION
    ${KDE_INSTALL_DBUSINTERFACEDIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kwin_export.h DESTINATION ${KDE_INSTALL_INCLUDEDIR} COMPONENT Devel)

include(CMakePackageConfigHelpers)
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KWinDBusInterface")

configure_package_config_file(
  desktop/kde/dbus/KWinDBusInterfaceConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/desktop/kde/dbus/KWinDBusInterfaceConfig.cmake"
  PATH_VARS KDE_INSTALL_DBUSINTERFACEDIR
  INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/desktop/kde/dbus/KWinDBusInterfaceConfig.cmake
  DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

ecm_install_configured_files(
  INPUT
    desktop/kde/plasma-kwin_x11.service.in
    desktop/kde/plasma-kwin_wayland.service.in
  @ONLY DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR}
)
