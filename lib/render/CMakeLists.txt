# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(render SHARED)

target_link_libraries(render
  PUBLIC
    win
    kwinglutils
    KF6::Notifications
    KF6::Plasma
)

target_sources(render
  PRIVATE
    post/color_correct_dbus_interface.cpp
    post/suncalc.cpp
    compositor_qobject.cpp
    dbus/compositing.cpp
    effect/basic_effect_loader.cpp
    effect/frame.cpp
    effect_loader.cpp
    effects.cpp
    gl/context_attribute_builder.cpp
    gl/egl_context_attribute_builder.cpp
    options.cpp
    outline.cpp
    singleton_interface.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    cursor.h
)

qt6_add_resources(render gl/resources.qrc)
kconfig_add_kcfg_files(render config/render_settings.kcfgc)
kconfig_add_kcfg_files(render post/kconfig/color_correct_settings.kcfgc)

qt6_add_dbus_adaptor(render_dbus_SRCS
  dbus/org.kde.kwin.Compositing.xml
  dbus/compositing.h
  KWin::render::dbus::compositing_qobject
)
qt6_add_dbus_adaptor(render_dbus_SRCS
  dbus/org.kde.kwin.ColorCorrect.xml
  post/color_correct_dbus_interface.h
  KWin::render::post::color_correct_dbus_interface
)
qt6_add_dbus_adaptor(render_dbus_SRCS
  ../effect/org.kde.kwin.Effects.xml
  effects.h
  KWin::render::effects_handler_wrap
)

target_sources(render
  PRIVATE
    ${render_dbus_SRCS}
)

set_target_properties(render PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-render"
)

add_library(render-x11 SHARED)

target_link_libraries(render-x11
  PUBLIC
    base-x11
    render
)

target_sources(render-x11
  PRIVATE
    x11/compositor_selection_owner.cpp
)

set_target_properties(render-x11 PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-render-x11"
)

install(
  FILES post/kconfig/color_correct_settings.kcfg
  DESTINATION ${KDE_INSTALL_KCFGDIR}
  RENAME ${KWIN_NAME}_colorcorrect.kcfg
)
install(
  FILES
    dbus/org.kde.kwin.ColorCorrect.xml
    dbus/org.kde.kwin.Compositing.xml
  DESTINATION
    ${KDE_INSTALL_DBUSINTERFACEDIR}
)

install(DIRECTORY qml/outline/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/outline)
install(DIRECTORY qml/frames/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/frames)

install(TARGETS render ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS render-x11 ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
