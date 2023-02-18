# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(blendchanges_SOURCES
    main.cpp
    blendchanges.cpp
)

kwin4_add_effect_module(kwin4_effect_blend ${blendchanges_SOURCES})
target_link_libraries(kwin4_effect_blend PRIVATE
    kwineffects
    kwinglutils
    Qt::DBus
)
