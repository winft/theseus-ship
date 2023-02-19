# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

integrationTest(NAME testScriptingScreenEdge SRCS screenedge_test.cpp)
integrationTest(WAYLAND_ONLY NAME testMinimizeAllScript SRCS minimizeall_test.cpp)
