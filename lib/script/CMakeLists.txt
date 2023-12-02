# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(script SHARED)
add_library(kwinft::script ALIAS script)

target_link_libraries(script
  PUBLIC
    KF6::ConfigQml
    base
    render
    win
)

qt6_add_dbus_adaptor(script_dbus_srcs
  org.kde.kwin.Script.xml
  script.h
  KWin::scripting::abstract_script
)

target_sources(script
  PUBLIC
    FILE_SET HEADERS
    FILES
      desktop_background_item.h
      effect.h
      effect_loader.h
      gesture_handler.h
      js_engine_global_methods_wrapper.h
      options.h
      output.h
      platform.h
      quick_scene_effect.h
      screen_edge_handler.h
      script.h
      scripting_logging.h
      script_timer.h
      shortcut_handler.h
      singleton_interface.h
      space.h
      utils.h
      virtual_desktop_model.h
      window.h
      window_model.h
      window_thumbnail_item.h
  PRIVATE
    dbus_call.cpp
    desktop_background_item.cpp
    js_engine_global_methods_wrapper.cpp
    screen_edge_handler.cpp
    effect.cpp
    gesture_handler.cpp
    options.cpp
    output.cpp
    platform.cpp
    quick_scene_effect.cpp
    script.cpp
    script_timer.cpp
    scripting_logging.cpp
    shortcut_handler.cpp
    singleton_interface.cpp
    utils.cpp
    virtual_desktop_model.cpp
    window.cpp
    window_model.cpp
    window_thumbnail_item.cpp
    space.cpp
    ${script_dbus_srcs}
)

set_target_properties(script PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  PREFIX libkwinft-
)

install(TARGETS script
  EXPORT kwinft-export
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/script
)
