# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(desktop SHARED)

target_link_libraries(desktop
  PUBLIC
    base
)

qt6_add_dbus_interface(
  desktop_dbus_src
  ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/kf6_org.freedesktop.ScreenSaver.xml
  screenlocker_interface
)
qt6_add_dbus_interface(
  desktop_dbus_src
  ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/org.kde.screensaver.xml
  kscreenlocker_interface
)

target_sources(desktop
  PRIVATE
    screen_locker_watcher.cpp
    ${desktop_dbus_src}
)

set_target_properties(desktop PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-desktop"
)

add_library(desktop-kde SHARED)

qt6_add_dbus_adaptor(desktop_kde_dbus_src
  kde/dbus/org.kde.KWin.xml
  kde/dbus/kwin.h
  KWin::desktop::kde::kwin
)

target_link_libraries(desktop-kde
  PUBLIC
    # TODO(romangg): Should not depend on debug.
    debug-common
    desktop
    win
)

target_sources(desktop-kde
  PRIVATE
    kde/dbus/kwin.cpp
    ${desktop_kde_dbus_src}
)

set_target_properties(desktop-kde PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-desktop-kde"
)

# TODO(romangg): Below should only be installed as part of a consumer install linking desktop-kde.
install(FILES kde/kwin.notifyrc
  DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
  RENAME ${KWIN_NAME}.notifyrc
)
install(
  FILES
    kde/dbus/org.kde.KWin.xml

    # Still needs to be installed as plasma-workspace expects it at compile time.
    kde/org.kde.kwin.VirtualKeyboard.xml
  DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR}
)

include(CMakePackageConfigHelpers)
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KWinDBusInterface")

configure_package_config_file(
  kde/dbus/KWinDBusInterfaceConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/kde/dbus/KWinDBusInterfaceConfig.cmake"
  PATH_VARS KDE_INSTALL_DBUSINTERFACEDIR
  INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/kde/dbus/KWinDBusInterfaceConfig.cmake
  DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

ecm_install_configured_files(
  INPUT
    kde/plasma-kwin_x11.service.in
    kde/plasma-kwin_wayland.service.in
  @ONLY DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR}
)

install(TARGETS desktop ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS desktop-kde ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
