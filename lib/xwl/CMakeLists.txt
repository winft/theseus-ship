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
    sources_ext.cpp
    transfer.cpp
)

set_target_properties(xwl PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  OUTPUT_NAME "kwin-xwl"
)

install(TARGETS xwl
  EXPORT xwl-export
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/xwl
)
install(EXPORT xwl-export
  NAMESPACE kwinft::
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)
