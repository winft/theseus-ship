# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(colorpicker_SOURCES
    colorpicker.cpp
    main.cpp
)

kwin4_add_effect_module(kwin4_effect_colorpicker ${colorpicker_SOURCES})
target_link_libraries(kwin4_effect_colorpicker PRIVATE
  KF6::I18n
  kwineffects
  Qt::DBus
)
