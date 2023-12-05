# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(xwayland SHARED)
add_library(kwinft::xwayland ALIAS xwayland)

target_link_libraries(xwayland
  PUBLIC
    base-x11
    render-x11
    win-x11
    WraplandServer
    XCB::CURSOR
)

target_sources(xwayland
  PUBLIC
    FILE_SET HEADERS
    FILES
      clipboard.h
      data_bridge.h
      dnd.h
      drag.h
      drag_wl.h
      drag_x.h
      event_x11.h
      mime.h
      primary_selection.h
      selection_data.h
      selection_wl.h
      selection_x11.h
      socket.h
      sources.h
      sources_ext.h
      surface.h
      transfer.h
      transfer_timeout.h
      types.h
      wl_visit.h
      x11_visit.h
      xwayland.h
  PRIVATE
    socket.cpp
    sources_ext.cpp
    transfer.cpp
)

set_target_properties(xwayland PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  PREFIX libkwinft-
)

install(TARGETS xwayland
  EXPORT kwinft-export
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/xwl
)
