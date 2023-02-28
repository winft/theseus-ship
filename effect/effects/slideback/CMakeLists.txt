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

kwin4_add_effect_module(slideback ${slideback_SOURCES})
target_link_libraries(slideback PRIVATE
    kwineffects
)
