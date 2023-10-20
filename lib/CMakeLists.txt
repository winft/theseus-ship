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

add_library(wlroots SHARED)

target_link_libraries(wlroots
  PUBLIC
    debug-wl
    input-wl
    win-x11

    # TODO(romangg): Linking against script lib is needed at the moment because in the base platform
    #                the script platform is composited, but in the future it should be made possible
    #                to build the wlroots backend lib without it.
    script

    Libinput::Libinput
    Pixman::Pixman
    wlroots::wlroots
    Wayland::Server
    WraplandClient
    xwl
)

target_sources(wlroots
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

set_target_properties(wlroots PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   PREFIX libkwinft-
)

target_compile_definitions(wlroots PUBLIC WLR_USE_UNSTABLE)

kcoreaddons_target_static_plugins(wlroots
  NAMESPACE "kwin/effects/plugins"
)

install(TARGETS wlroots
  EXPORT kwinft-export
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft
)

install(EXPORT kwinft-export
  NAMESPACE kwinft::
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)
