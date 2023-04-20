# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(kwindowsystem_plugin_SRCS
    plugin.cpp
    windoweffects.cpp
    windowsystem.cpp
)

add_library(KF6WindowSystemKWinPlugin OBJECT ${kwindowsystem_plugin_SRCS})
target_compile_definitions(KF6WindowSystemKWinPlugin PRIVATE QT_STATICPLUGIN)
target_link_libraries(KF6WindowSystemKWinPlugin
PRIVATE
  kwin
  KF6::WindowSystem
)
