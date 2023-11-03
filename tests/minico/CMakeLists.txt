# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(minico main.cpp)

target_link_libraries(minico
  kwinft::wayland
  KF6IdleTimeKWinPlugin
  KF6WindowSystemKWinPlugin
  KWinQpaPlugin
  KF6::Crash
)

install(TARGETS minico)
