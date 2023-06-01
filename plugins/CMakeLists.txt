# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_subdirectory(effects)
add_subdirectory(idletime)
add_subdirectory(kpackage)
add_subdirectory(scripts)
add_subdirectory(qpa)
add_subdirectory(windowsystem)

if (KWIN_BUILD_DECORATIONS)
    add_subdirectory(kdecorations)
endif()
