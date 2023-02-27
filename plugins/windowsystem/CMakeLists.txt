# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(kwindowsystem_plugin_SRCS
    plugin.cpp
    windoweffects.cpp
    windowsystem.cpp
)

add_library(KF5WindowSystemKWinPlugin OBJECT ${kwindowsystem_plugin_SRCS})
target_compile_definitions(KF5WindowSystemKWinPlugin PRIVATE QT_STATICPLUGIN)
target_link_libraries(KF5WindowSystemKWinPlugin
PRIVATE
  kwin
  KF5::WindowSystem
)
