# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_subdirectory(utils)
add_subdirectory(base)
add_subdirectory(win)
add_subdirectory(input)
add_subdirectory(render)
add_subdirectory(data)
add_subdirectory(script)
add_subdirectory(xwl)
add_subdirectory(debug)
add_subdirectory(desktop)

add_library(x11 SHARED)
add_library(kwinft::x11 ALIAS x11)

target_link_libraries(x11
  PUBLIC
    base-x11
    debug-common
    desktop
    input-x11-backend
    render-x11-backend

    # TODO(romangg): Linking against script lib is needed at the moment because in the base platform
    #                the script platform is composited, but in the future it should be made possible
    #                to build the wlroots backend lib without it.
    script
    win-x11-backend
)

target_sources(x11
  PUBLIC
    FILE_SET HEADERS
    FILES
      base/x11/platform.h
)

add_library(wayland SHARED)
add_library(kwinft::wayland ALIAS wayland)

target_link_libraries(wayland
  PUBLIC
    debug-wl
    desktop
    input-wl
    render-wl
    win-wl

    # TODO(romangg): Linking against script lib is needed at the moment because in the base platform
    #                the script platform is composited, but in the future it should be made possible
    #                to build the wlroots backend lib without it.
    script

    Libinput::Libinput
    Pixman::Pixman
    wlroots::wlroots
    Wayland::Server
)

target_sources(wayland
  PUBLIC
    FILE_SET HEADERS
    FILES
      base/backend/wlroots/drm_lease.h
      base/backend/wlroots/non_desktop_output.h
      base/backend/wlroots/output.h
      base/backend/wlroots/platform.h
      base/backend/wlroots/platform_events.h
      base/backend/wlroots/platform_helpers.h
      input/backend/wlroots/device_helpers.h
      input/backend/wlroots/keyboard.h
      input/backend/wlroots/platform.h
      input/backend/wlroots/pointer.h
      input/backend/wlroots/switch.h
      input/backend/wlroots/touch.h
      render/backend/wlroots/egl_backend.h
      render/backend/wlroots/egl_helpers.h
      render/backend/wlroots/egl_output.h
      render/backend/wlroots/egl_texture.h
      render/backend/wlroots/output.h
      render/backend/wlroots/output_event.h
      render/backend/wlroots/platform.h
      render/backend/wlroots/qpainter_backend.h
      render/backend/wlroots/qpainter_output.h
      render/backend/wlroots/texture_update.h
      render/backend/wlroots/wlr_helpers.h
      render/backend/wlroots/wlr_includes.h
      render/backend/wlroots/wlr_non_owning_data_buffer.h
  PRIVATE
   base/backend/wlroots/drm_lease.cpp
   base/seat/backend/wlroots/session.cpp
   input/backend/wlroots/control/headless/keyboard.cpp
   input/backend/wlroots/control/keyboard.cpp
   input/backend/wlroots/control/pointer.cpp
   input/backend/wlroots/control/switch.cpp
   input/backend/wlroots/control/touch.cpp
)

set_target_properties(x11 wayland PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   PREFIX libkwinft-
)

target_compile_definitions(wayland PUBLIC WLR_USE_UNSTABLE)

kcoreaddons_target_static_plugins(wayland
  NAMESPACE "kwin/effects/plugins"
)

install(TARGETS x11 wayland
  EXPORT kwinft-export
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft
)

install(EXPORT kwinft-export
  NAMESPACE kwinft::
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)
