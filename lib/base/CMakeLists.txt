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
  PRIVATE
    os/clock/skew_notifier.cpp
    os/clock/skew_notifier_engine.cpp
    seat/session.cpp
    seat/backend/logind/session.cpp
    singleton_interface.cpp
    logging.cpp
    options.cpp
    platform.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    output.h
)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
  target_sources(base
    PRIVATE
      os/clock/linux_skew_notifier_engine.cpp
  )
endif()

configure_file(config-kwin.h.cmake config-kwin.h)

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
  PRIVATE
    app_singleton.cpp
    wayland/filtered_display.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    wayland/server.h
)

set_target_properties(base-wl PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   OUTPUT_NAME "kwin-base-wl"
)

install(TARGETS base ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS base-x11 ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS base-wl ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
