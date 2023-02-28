# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

# Source files
set(snaphelper_SOURCES
    main.cpp
    snaphelper.cpp
)

kwin4_add_effect_module(snaphelper ${snaphelper_SOURCES})
target_link_libraries(snaphelper PRIVATE
    kwineffects
    kwinglutils
)
