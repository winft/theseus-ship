# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_subdirectory(base)
add_subdirectory(effect)
add_subdirectory(win)
add_subdirectory(input)
add_subdirectory(render)
add_subdirectory(data)
add_subdirectory(script)
add_subdirectory(xwl)
add_subdirectory(debug)
add_subdirectory(desktop)

### Common library

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
    debug-common
    desktop-kde
    input
    render
    script
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

generate_export_header(kwin EXPORT_FILE_NAME kwin_export.h)

### X11 library
set(kwin_X11_SRCS
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
  render-x11
  KF6::Crash
PRIVATE
  Qt::GuiPrivate
  X11::Xi
)

if (HAVE_DL_LIBRARY)
    target_link_libraries(kwin_x11_lib PRIVATE ${DL_LIBRARY})
endif()


add_library(wlroots SHARED)

target_link_libraries(wlroots
  PUBLIC
    debug-wl
    input-wl
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
      base/backend/wlroots/platform_helpers.h
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
   base/backend/wlroots/non_desktop_output.cpp
   base/backend/wlroots/output.cpp
   base/backend/wlroots/platform.cpp
   base/seat/backend/wlroots/session.cpp
   input/backend/wlroots/platform.cpp
   input/backend/wlroots/control/headless/keyboard.cpp
   input/backend/wlroots/control/keyboard.cpp
   input/backend/wlroots/control/pointer.cpp
   input/backend/wlroots/control/switch.cpp
   input/backend/wlroots/control/touch.cpp
)

set_target_properties(wlroots PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   OUTPUT_NAME "kwin-wlroots"
)

target_compile_definitions(wlroots PUBLIC WLR_USE_UNSTABLE)

kcoreaddons_target_static_plugins(
  wlroots "kwin/effects/plugins"
  LINK_OPTION "PRIVATE"
)

install(TARGETS kwin ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)

install(TARGETS wlroots
  EXPORT wlroots-export
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft
)
install(EXPORT wlroots-export
  NAMESPACE kwinft::
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kwin_export.h DESTINATION ${KDE_INSTALL_INCLUDEDIR} COMPONENT Devel)
