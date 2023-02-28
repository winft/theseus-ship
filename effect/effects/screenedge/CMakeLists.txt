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

kwin4_add_effect_module(screenedge ${screenedge_SOURCES})
target_link_libraries(screenedge PRIVATE
  KF6::Plasma
  kwineffects
  kwinglutils
)
