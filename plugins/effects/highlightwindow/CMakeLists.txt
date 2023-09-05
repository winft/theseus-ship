# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

# Source files
set(highlightwindow_SOURCES
    highlightwindow.cpp
    main.cpp
)

kwin_add_builtin_effect(highlightwindow ${highlightwindow_SOURCES})
target_link_libraries(highlightwindow PRIVATE
  Qt::DBus
  render
)
