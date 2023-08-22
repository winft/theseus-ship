# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

# Source files
set(screenedge_SOURCES
    main.cpp
    screenedgeeffect.cpp
)

kwin_add_builtin_effect(screenedge ${screenedge_SOURCES})
target_link_libraries(screenedge PRIVATE
  KF6::ConfigCore
  KF6::Svg
  render
)
