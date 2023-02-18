# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_subdirectory(kglobalaccel)
add_subdirectory(qpa)
add_subdirectory(idletime)
add_subdirectory(windowsystem)
add_subdirectory(kpackage)

if (KWIN_BUILD_DECORATIONS)
    add_subdirectory(kdecorations)
endif()
