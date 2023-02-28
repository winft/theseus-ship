# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

# Source files
set(contrast_SOURCES
    contrast.cpp
    contrastshader.cpp
    main.cpp
)
kwin4_add_effect_module(contrast ${contrast_SOURCES})
target_link_libraries(contrast PRIVATE
    kwineffects
    kwinglutils
)
