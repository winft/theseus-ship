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
add_subdirectory(data)
add_subdirectory(script)

# Target killer helper
add_executable(kwin_killer_helper win/x11/helpers/killer.cpp)

target_link_libraries(kwin_killer_helper
  KF6::AuthCore
  KF6::I18n
  KF6::WidgetsAddons
  Qt::GuiPrivate
  Qt::Widgets
)

### Common library
set(kwin_SRCS
    desktop/kde/dbus/kwin.cpp
    desktop/screen_locker_watcher.cpp
    render/post/color_correct_dbus_interface.cpp
    render/post/suncalc.cpp
    debug/console/console.cpp
    debug/perf/ftrace.cpp
    input/keyboard.cpp
    input/keyboard_redirect.cpp
    input/platform_qobject.cpp
    input/redirect_qobject.cpp
    input/singleton_interface.cpp
    input/spies/modifier_only_shortcuts.cpp
    input/switch.cpp
    input/cursor.cpp
    input/cursor_shape.cpp
    input/gestures.cpp
    input/global_shortcut.cpp
    input/logging.cpp
    input/pointer.cpp
    input/xkb/keyboard.cpp
    input/xkb/keymap.cpp
    render/compositor_qobject.cpp
    render/dbus/compositing.cpp
    render/effect/frame.cpp
    render/effect_loader.cpp
    render/effects.cpp
    render/gl/context_attribute_builder.cpp
    render/gl/egl_context_attribute_builder.cpp
    render/options.cpp
    render/outline.cpp
    render/singleton_interface.cpp
    render/window_thumbnail_item.cpp
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
    script/space.cpp
    win/dbus/appmenu.cpp
    win/dbus/virtual_desktop_manager.cpp
    win/dbus/virtual_desktop_types.cpp
    win/deco/palette.cpp
    win/deco/decorations_logging.cpp
    win/options.cpp
    win/osd_notification.cpp
    win/property_window.cpp
    win/rules/book.cpp
    win/rules/book_settings.cpp
    win/rules/ruling.cpp
    win/rules/window.cpp
    win/session_manager.cpp
    win/screen_edges.cpp
    win/shortcut_dialog.cpp
    win/singleton_interface.cpp
    win/space_qobject.cpp
    win/virtual_desktops.cpp
    win/x11/client_machine.cpp
    win/x11/extras.cpp
    win/x11/key_server.cpp
    win/x11/net/atoms.cpp
    win/x11/net/root_info.cpp
    win/x11/net/win_info.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    render/cursor.h
    win/deco/bridge_qobject.h
    win/deco/client_impl_qobject.h
    win/deco/renderer.h
    win/stacking_order.h
    win/window_qobject.h
)

qt6_add_resources(kwin_SRCS render/gl/resources.qrc)

qt6_add_dbus_adaptor(kwin_SRCS
  script/org.kde.kwin.Script.xml
  script/script.h
  KWin::scripting::abstract_script
)

kconfig_add_kcfg_files(kwin_SRCS win/config/win_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS render/config/render_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS render/post/kconfig/color_correct_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS win/rules/kconfig/rules_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS win/rules/kconfig/rules_book_settings_base.kcfgc)

qt6_add_dbus_adaptor(kwin_SRCS
  desktop/kde/dbus/org.kde.KWin.xml
  desktop/kde/dbus/kwin.h
  KWin::desktop::kde::kwin
)
qt6_add_dbus_adaptor(kwin_SRCS
  render/dbus/org.kde.kwin.Compositing.xml
  render/dbus/compositing.h
  KWin::render::dbus::compositing_qobject
)
qt6_add_dbus_adaptor(kwin_SRCS
  render/dbus/org.kde.kwin.ColorCorrect.xml
  render/post/color_correct_dbus_interface.h
  KWin::render::post::color_correct_dbus_interface
)
qt6_add_dbus_adaptor(kwin_SRCS ${kwin_effects_dbus_xml} render/effects.h KWin::render::effects_handler_wrap)
qt6_add_dbus_adaptor(kwin_SRCS
    win/dbus/org.kde.KWin.VirtualDesktopManager.xml
    win/dbus/virtual_desktop_manager.h
    KWin::win::dbus::virtual_desktop_manager
)
qt6_add_dbus_adaptor(kwin_SRCS
  win/dbus/org.kde.KWin.Session.xml
  win/session_manager.h
  KWin::win::session_manager
)

qt6_add_dbus_interface(kwin_SRCS ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/kf6_org.freedesktop.ScreenSaver.xml screenlocker_interface)
qt6_add_dbus_interface(kwin_SRCS ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/org.kde.screensaver.xml kscreenlocker_interface)
qt6_add_dbus_interface(kwin_SRCS win/dbus/org.kde.kappmenu.xml appmenu_interface)

ki18n_wrap_ui(kwin_SRCS
    debug/console/debug_console.ui
    win/shortcut_dialog.ui
)

set(kwin_QT_LIBS
    Qt::Concurrent
    Qt::DBus
    Qt::Quick
)

set(kwin_KDE_LIBS
  KF6::ConfigCore
  KF6::ConfigWidgets
  KF6::CoreAddons
  KF6::I18n
  KF6::Notifications
  KF6::Package
  KF6::Plasma
  KF6::Service

  KDecoration2::KDecoration
  KDecoration2::KDecoration2Private
)

set(kwin_XCB_LIBS
    XCB::CURSOR
    XCB::ICCCM
    XCB::KEYSYMS
    XCB::RENDER
    XCB::SHM
    XCB::XFIXES
)

set(kwin_INPUT_LIBS
  XKB::XKB
)

set(kwinLibs
    base
    kwineffects
    ${kwin_QT_LIBS}
    ${kwin_KDE_LIBS}
    ${X11_LIBRARIES}
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
target_link_libraries(kwin ${kwinLibs} kwinglutils epoxy::epoxy)

if(HAVE_PERF)
    target_sources(kwin PRIVATE debug/perf/ftrace_impl.cpp)
endif()

if (KWIN_BUILD_TABBOX)
  target_sources(kwin PRIVATE
    win/tabbox/tabbox_client_model.cpp
    win/tabbox/tabbox_config.cpp
    win/tabbox/tabbox_desktop_chain.cpp
    win/tabbox/tabbox_desktop_model.cpp
    win/tabbox/tabbox_handler.cpp
    win/tabbox/tabbox_logging.cpp
    win/tabbox/tabbox_switcher_item.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    win/tabbox/tabbox.h
  )
  target_link_libraries(kwin Qt::GuiPrivate)
endif()

generate_export_header(kwin EXPORT_FILE_NAME kwin_export.h)

### X11 library
set(kwin_X11_SRCS
   base/backend/x11/platform.cpp
   base/x11/output.cpp
   input/x11/cursor.cpp
   input/x11/global_shortcuts_manager.cpp
   input/x11/xfixes_cursor_event_filter.cpp
   input/logging.cpp
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
  KF6::Crash
  KF6::GlobalAccel
PRIVATE
  Qt::GuiPrivate
  X11::Xi
)

if (HAVE_DL_LIBRARY)
    target_link_libraries(kwin_x11_lib PRIVATE ${DL_LIBRARY})
endif()

### Wayland library
set(kwin_XWAYLAND_SRCS
   xwl/sources_ext.cpp
   xwl/transfer.cpp

   # These headers need to be compiled for the helper QObjects they contain.
   xwl/drag.h
   xwl/selection_data.h
   xwl/sources.h
   xwl/wl_visit.h
   xwl/x11_visit.h
)

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
   input/control/device.cpp
   input/control/keyboard.cpp
   input/control/pointer.cpp
   input/control/switch.cpp
   input/control/touch.cpp
   input/dbus/device.cpp
   input/dbus/device_manager.cpp
   input/dbus/keyboard_layout.cpp
   input/dbus/keyboard_layouts_v2.cpp
   input/dbus/tablet_mode_manager.cpp
   input/spies/keyboard_repeat.cpp
   input/wayland/global_shortcuts_manager.cpp
   input/logging.cpp
   input/wayland/kglobalaccel/runtime/component.cpp
   input/wayland/kglobalaccel/runtime/global_accel_d.cpp
   input/wayland/kglobalaccel/runtime/global_shortcut.cpp
   input/wayland/kglobalaccel/runtime/global_shortcut_context.cpp
   input/wayland/kglobalaccel/runtime/global_shortcuts_registry.cpp
   input/wayland/kglobalaccel/runtime/service_action_component.cpp
   input/wayland/kglobalaccel/runtime/sequence_helpers.cpp
   input/xkb/layout_manager.cpp
   input/xkb/layout_policies.cpp

   # These headers need to be compiled for the helper QObjects they contain.
   input/touch.h
   input/wayland/cursor_image.h
   input/wayland/cursor_theme.h
)

if (KWIN_BUILD_TABBOX)
  set(kwin_WAYLAND_SRCS
    ${kwin_WAYLAND_SRCS}
  )
endif()

add_library(kwin_wayland_lib SHARED
  ${kwin_XWAYLAND_SRCS}
  ${kwin_WAYLAND_SRCS}
)

target_link_libraries(kwin_wayland_lib
PUBLIC
  kwin
  KF6::GlobalAccel
  WraplandClient
  Wayland::Server
  Wayland::Cursor
  wlroots::wlroots
  Pixman::Pixman
  Libinput::Libinput
  Threads::Threads
  base-wl
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
install(TARGETS kwin_killer_helper DESTINATION ${KDE_INSTALL_LIBEXECDIR})
install(TARGETS kwin ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS kwin_wayland_lib ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)

install(FILES render/post/kconfig/color_correct_settings.kcfg
    DESTINATION ${KDE_INSTALL_KCFGDIR}
    RENAME ${KWIN_NAME}_colorcorrect.kcfg
)
install(FILES desktop/kde/kwin.notifyrc
  DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
  RENAME ${KWIN_NAME}.notifyrc
)
install(
  FILES
    win/dbus/org.kde.KWin.VirtualDesktopManager.xml
    desktop/kde/dbus/org.kde.KWin.xml
    render/dbus/org.kde.kwin.ColorCorrect.xml
    render/dbus/org.kde.kwin.Compositing.xml
    render/dbus/org.kde.kwin.Effects.xml

    # Still needs to be installed as plasma-workspace expects it at compile time.
    data/org.kde.kwin.VirtualKeyboard.xml
  DESTINATION
    ${KDE_INSTALL_DBUSINTERFACEDIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kwin_export.h DESTINATION ${KDE_INSTALL_INCLUDEDIR} COMPONENT Devel)

# Install QML files
install(DIRECTORY render/qml/outline/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/outline)
install(DIRECTORY render/qml/frames/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/frames)
install(
  DIRECTORY win/qml/onscreennotification/plasma
  DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/onscreennotification
)

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

qt6_generate_dbus_interface(input/dbus/device.h org.kde.kwin.InputDevice.xml OPTIONS -A)

add_custom_target(KWinInputDBusInterfaces ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
  DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR}
)
