# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(win SHARED)

target_link_libraries(win
  PUBLIC
    base
    kwineffects
    KDecoration2::KDecoration
    KDecoration2::KDecoration2Private
    KF6::ConfigWidgets
    KF6::CoreAddons
    KF6::I18n
    KF6::Package
    Qt::Concurrent
    Qt::OpenGL
    Qt::Quick
    XCB::KEYSYMS
    ${X11_LIBRARIES}
)

target_sources(win
  PRIVATE
    cursor_shape.cpp
    dbus/appmenu.cpp
    dbus/virtual_desktop_manager.cpp
    dbus/virtual_desktop_types.cpp
    deco/palette.cpp
    deco/decorations_logging.cpp
    input/gestures.cpp
    input/global_shortcut.cpp
    options.cpp
    osd_notification.cpp
    property_window.cpp
    rules/book.cpp
    rules/book_settings.cpp
    rules/ruling.cpp
    rules/window.cpp
    session_manager.cpp
    screen_edges.cpp
    shortcut_dialog.cpp
    singleton_interface.cpp
    space_qobject.cpp
    virtual_desktops.cpp
    x11/client_machine.cpp
    x11/extras.cpp
    x11/key_server.cpp
    x11/net/atoms.cpp
    x11/net/root_info.cpp
    x11/net/win_info.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    deco/bridge_qobject.h
    deco/client_impl_qobject.h
    deco/renderer.h
    stacking_order.h
    window_qobject.h
)

if (KWIN_BUILD_TABBOX)
  target_sources(win
    PRIVATE
      tabbox/tabbox_client_model.cpp
      tabbox/tabbox_config.cpp
      tabbox/tabbox_desktop_chain.cpp
      tabbox/tabbox_desktop_model.cpp
      tabbox/tabbox_handler.cpp
      tabbox/tabbox_logging.cpp
      tabbox/tabbox_switcher_item.cpp

      # These headers need to be compiled for the helper QObjects they contain.
      tabbox/tabbox.h
  )
  target_link_libraries(win PRIVATE Qt::GuiPrivate)
endif()

qt6_add_dbus_adaptor(win_dbus_src
  dbus/org.kde.KWin.VirtualDesktopManager.xml
  dbus/virtual_desktop_manager.h
  KWin::win::dbus::virtual_desktop_manager
)
qt6_add_dbus_adaptor(win_dbus_src
  dbus/org.kde.KWin.Session.xml
  session_manager.h
  KWin::win::session_manager
)
qt6_add_dbus_interface(win_dbus_src dbus/org.kde.kappmenu.xml appmenu_interface)
qt6_add_dbus_interface(win_dbus_src
  ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/kf6_org.freedesktop.ScreenSaver.xml
  screenlocker_interface
)

target_sources(win PRIVATE ${win_dbus_src})

kconfig_add_kcfg_files(win config/win_settings.kcfgc)
kconfig_add_kcfg_files(win rules/kconfig/rules_settings.kcfgc)
kconfig_add_kcfg_files(win rules/kconfig/rules_book_settings_base.kcfgc)

ki18n_wrap_ui(win shortcut_dialog.ui)

set_target_properties(win PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-win"
)

# Target killer helper
add_executable(kwin_killer_helper x11/helpers/killer.cpp)

target_link_libraries(kwin_killer_helper
  KF6::AuthCore
  KF6::I18n
  KF6::WidgetsAddons
  Qt::GuiPrivate
  Qt::Widgets
)

install(
  FILES dbus/org.kde.KWin.VirtualDesktopManager.xml
  DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR}
)
install(
  DIRECTORY qml/onscreennotification/plasma
  DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/onscreennotification
)
install(TARGETS win ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS kwin_killer_helper DESTINATION ${KDE_INSTALL_LIBEXECDIR})
