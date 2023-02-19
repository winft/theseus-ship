# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

# Source files
set(slideback_SOURCES
    main.cpp
    slideback.cpp
)

kwin4_add_effect_module(kwin4_effect_slideback ${slideback_SOURCES})
target_link_libraries(kwin4_effect_slideback PRIVATE
    kwineffects
)
