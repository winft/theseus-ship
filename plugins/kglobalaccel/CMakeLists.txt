# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(kglobalaccel_plugin_SRCS
    kglobalaccel_plugin.cpp
)

add_library(KF5GlobalAccelKWinPlugin OBJECT ${kglobalaccel_plugin_SRCS})
target_compile_definitions(KF5GlobalAccelKWinPlugin PRIVATE QT_STATICPLUGIN)
target_link_libraries(KF5GlobalAccelKWinPlugin KF5::GlobalAccelPrivate kwin)
