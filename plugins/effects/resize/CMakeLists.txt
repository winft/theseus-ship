# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(resize_SOURCES
    main.cpp
    resize.cpp
)

kconfig_add_kcfg_files(resize_SOURCES
    resizeconfig.kcfgc
)

kwin_add_builtin_effect(resize ${resize_SOURCES})
target_link_libraries(resize PRIVATE
  KF6::KCMUtils
  kwineffects
)

#######################################
# Config
set(kwin_resize_config_SRCS resize_config.cpp)
ki18n_wrap_ui(kwin_resize_config_SRCS resize_config.ui)
kconfig_add_kcfg_files(kwin_resize_config_SRCS resizeconfig.kcfgc)

kwin_add_effect_config(kwin_resize_config ${kwin_resize_config_SRCS})
