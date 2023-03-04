# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

if (XCB_ICCCM_FOUND)
    set(normalhintsbasesizetest_SRCS normalhintsbasesizetest.cpp)
    add_executable(normalhintsbasesizetest ${normalhintsbasesizetest_SRCS})
    target_link_libraries(normalhintsbasesizetest XCB::XCB XCB::ICCCM)
endif()

# next target
set(screenedgeshowtest_SRCS screenedgeshowtest.cpp)
add_executable(screenedgeshowtest ${screenedgeshowtest_SRCS})
target_link_libraries(screenedgeshowtest
  Qt::Widgets
  Qt::X11Extras
  KF5::ConfigCore
  KF5::WindowSystem
  WraplandClient
  ${XCB_XCB_LIBRARY}
)

add_executable(x11shadowreader x11shadowreader.cpp)
target_link_libraries(x11shadowreader XCB::XCB Qt::Widgets Qt::X11Extras KF5::ConfigCore)

add_executable(pointerconstraints pointerconstraintstest.cpp)
add_definitions(-DDIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pointerconstraints XCB::XCB Qt::Gui Qt::Quick WraplandClient)

add_executable(pointergestures pointergesturestest.cpp)
add_definitions(-DDIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pointergestures Qt::Gui Qt::Quick WraplandClient)

add_executable(cursorhotspottest cursorhotspottest.cpp)
target_link_libraries(cursorhotspottest Qt::Widgets)

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${KWIN_SOURCE_DIR})
