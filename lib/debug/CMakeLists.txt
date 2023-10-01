# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(debug-common SHARED)

target_link_libraries(debug-common
  PUBLIC
    base
    render
    win
)

target_sources(debug-common
  PUBLIC
    FILE_SET HEADERS
    FILES
      console/console.h
      console/model_helpers.h
      console/window.h
      perf/ftrace.h
      support_info.h
  PRIVATE
    console/console.cpp
    perf/ftrace.cpp
)

if(HAVE_PERF)
  target_sources(debug-common
    PUBLIC
      FILE_SET HEADERS
      FILES
        perf/ftrace_impl.h
    PRIVATE
      perf/ftrace_impl.cpp
  )
endif()

ki18n_wrap_ui(debug-common
  console/debug_console.ui
)

set_target_properties(debug-common PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-debug-common"
)

add_library(debug-wl SHARED)

target_link_libraries(debug-wl
  PUBLIC
    debug-common
    input-wl
)

target_sources(debug-wl
  PUBLIC
    FILE_SET HEADERS
    FILES
      console/wayland/input_device_model.h
      console/wayland/input_filter.h
      console/wayland/model_helpers.h
      console/wayland/surface_tree_model.h
      console/wayland/wayland_console.h
      console/wayland/xwl_console.h
      console/wayland/xwl_surface_tree_model.h
  PRIVATE
    console/wayland/input_device_model.cpp
    console/wayland/wayland_console.cpp
)

set_target_properties(debug-wl PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-debug-wl"
)

install(TARGETS debug-common
  EXPORT debug-common-export
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/debug
)
install(TARGETS debug-wl
  EXPORT debug-wl-export
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/debug
)

install(EXPORT debug-common-export
  NAMESPACE kwinft::
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)
install(EXPORT debug-wl-export
  NAMESPACE kwinft::
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)
