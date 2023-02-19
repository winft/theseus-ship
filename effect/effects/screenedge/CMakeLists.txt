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

kwin4_add_effect_module(kwin4_effect_screenedge ${screenedge_SOURCES})
target_link_libraries(kwin4_effect_screenedge PRIVATE
    kwineffects
    kwinglutils
    KF5::Plasma
)
