# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(touchpoints_SOURCES
    main.cpp
    touchpoints.cpp
)

kwin_add_builtin_effect(touchpoints ${touchpoints_SOURCES})
target_link_libraries(touchpoints PRIVATE
    kwineffects
    kwinglutils
    Qt::Widgets
)
