# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(input SHARED)

target_link_libraries(input
  PUBLIC
    win
    XCB::CURSOR
)

target_sources(input
  PRIVATE
    keyboard.cpp
    keyboard_redirect.cpp
    platform_qobject.cpp
    redirect_qobject.cpp
    singleton_interface.cpp
    spies/modifier_only_shortcuts.cpp
    switch.cpp
    cursor.cpp
    logging.cpp
    pointer.cpp
    xkb/keyboard.cpp
    xkb/keymap.cpp
)

set_target_properties(input PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-input"
)

add_library(input-x11 SHARED)

target_link_libraries(input-x11
  PUBLIC
    base-x11
    input
    KF6::GlobalAccel
)

target_sources(input-x11
  PRIVATE
    x11/cursor.cpp
    x11/global_shortcuts_manager.cpp
    x11/xfixes_cursor_event_filter.cpp
)

set_target_properties(input-x11 PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-input-x11"
)

add_library(input-wl SHARED)

target_link_libraries(input-wl
  PUBLIC
    base-wl
    input
    KF6::GlobalAccel
    Wayland::Cursor
)

target_sources(input-wl
  PRIVATE
    control/device.cpp
    control/keyboard.cpp
    control/pointer.cpp
    control/switch.cpp
    control/touch.cpp
    dbus/device.cpp
    dbus/device_manager.cpp
    dbus/keyboard_layout.cpp
    dbus/keyboard_layouts_v2.cpp
    dbus/tablet_mode_manager.cpp
    spies/keyboard_repeat.cpp
    wayland/global_shortcuts_manager.cpp
    logging.cpp
    wayland/kglobalaccel/runtime/component.cpp
    wayland/kglobalaccel/runtime/global_accel_d.cpp
    wayland/kglobalaccel/runtime/global_shortcut.cpp
    wayland/kglobalaccel/runtime/global_shortcut_context.cpp
    wayland/kglobalaccel/runtime/global_shortcuts_registry.cpp
    wayland/kglobalaccel/runtime/service_action_component.cpp
    wayland/kglobalaccel/runtime/sequence_helpers.cpp
    xkb/layout_manager.cpp
    xkb/layout_policies.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    touch.h
    wayland/cursor_image.h
    wayland/cursor_theme.h
)

set_target_properties(input-wl PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-input-wl"
)

qt6_generate_dbus_interface(dbus/device.h org.kde.kwin.InputDevice.xml OPTIONS -A)

add_custom_target(KWinInputDBusInterfaces ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
  DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR}
)

install(TARGETS input ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS input-x11 ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS input-wl ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
