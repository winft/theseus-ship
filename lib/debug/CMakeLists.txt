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
  PRIVATE
    console/console.cpp
    perf/ftrace.cpp
)

if(HAVE_PERF)
  target_sources(debug-common PRIVATE perf/ftrace_impl.cpp)
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
  PRIVATE
    console/wayland/input_device_model.cpp
    console/wayland/wayland_console.cpp
)

set_target_properties(debug-wl PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-debug-wl"
)

install(TARGETS debug-common ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS debug-wl ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
