# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(xwl SHARED)

target_link_libraries(xwl
  PUBLIC
    base-x11
    render-x11
    WraplandServer
)

target_sources(xwl
  PRIVATE
    sources_ext.cpp
    transfer.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    drag.h
    selection_data.h
    sources.h
    wl_visit.h
    x11_visit.h
)

set_target_properties(xwl PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-xwl"
)

install(TARGETS xwl ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
