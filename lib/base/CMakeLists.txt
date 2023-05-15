# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(base SHARED)

target_link_libraries(base
  PUBLIC
    Qt::Core
    Qt::DBus
    Qt::Gui
    Qt::GuiPrivate
    Qt::Widgets
    KF6::ConfigCore
)

target_sources(base
  PUBLIC
    FILE_SET HEADERS
    FILES
      os/clock/linux_skew_notifier_engine.h
      os/clock/skew_notifier.h
      seat/backend/logind/session.h
      seat/session.h
      app_singleton.h
      config.h
      logging.h
      options.h
      output.h
      output_helpers.h
      output_topology.h
      singleton_interface.h
      types.h
      utils.h
  PRIVATE
    app_singleton.cpp
    os/clock/skew_notifier.cpp
    os/clock/skew_notifier_engine.cpp
    seat/session.cpp
    seat/backend/logind/session.cpp
    singleton_interface.cpp
    logging.cpp
    options.cpp
    platform.cpp
)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
  target_sources(base
    PUBLIC
      FILE_SET HEADERS
      FILES
        os/clock/skew_notifier_engine.h
    PRIVATE
      os/clock/linux_skew_notifier_engine.cpp
  )
endif()

configure_file(config-kwin.h.cmake config-kwin.h)
generate_export_header(base BASE_NAME kwin)

set_target_properties(base PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   OUTPUT_NAME "kwin-base"
)

add_library(base-x11 SHARED)

target_link_libraries(base-x11
  PUBLIC
    base
    XCB::COMPOSITE
    XCB::DAMAGE
    XCB::GLX
    XCB::RANDR
    XCB::SHAPE
    XCB::SYNC
    XCB::XKB
)

target_sources(base-x11
  PUBLIC
    FILE_SET HEADERS
    # TODO(romangg): Should add BASE_DIRS but conflicts with xcb files shadowing system includes.
    FILES
      x11/atoms.h
      x11/data.h
      x11/event_filter.h
      x11/event_filter_container.h
      x11/event_filter_manager.h
      x11/fixx11h.h
      x11/grabs.h
      x11/platform.h
      x11/selection_owner.h
      x11/user_interaction_filter.h
      x11/xcb/atom.h
      x11/xcb/extensions.h
      x11/xcb/geometry_hints.h
      x11/xcb/helpers.h
      x11/xcb/motif_hints.h
      x11/xcb/property.h
      x11/xcb/proto.h
      x11/xcb/qt_types.h
      x11/xcb/randr.h
      x11/xcb/window.h
      x11/xcb/wrapper.h
  PRIVATE
    x11/event_filter.cpp
    x11/event_filter_container.cpp
    x11/event_filter_manager.cpp
    x11/grabs.cpp
    x11/selection_owner.cpp
    x11/xcb/extensions.cpp
)

set_target_properties(base-x11 PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   OUTPUT_NAME "kwin-base-x11"
)

add_library(base-wl SHARED)

target_link_libraries(base-wl
  PUBLIC
    KF6::Service
    PW::KScreenLocker
    WraplandServer
    base
)

target_sources(base-wl
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS wayland
    FILES
      wayland/filtered_display.h
      wayland/output.h
      wayland/output_helpers.h
      wayland/output_transform.h
      wayland/platform.h
      wayland/screen_lock.h
      wayland/server.h
  PRIVATE
    wayland/filtered_display.cpp
)

set_target_properties(base-wl PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   OUTPUT_NAME "kwin-base-wl"
)

install(TARGETS base
  EXPORT base-export
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/base
)
install(TARGETS base-x11
  EXPORT base-x11-export
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/base
)
install(TARGETS base-wl
  EXPORT base-wl-export
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/base/wl
)
install(EXPORT base-export
  NAMESPACE kwinft::
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)
install(EXPORT base-x11-export
  NAMESPACE kwinft::
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)
install(EXPORT base-wl-export
  NAMESPACE kwinft::
  ${INSTALL_TARGETS_DEFAULT_ARGS}
  DESTINATION ${KDE_INSTALL_CMAKEPACKAGEDIR}/kwinft
)
