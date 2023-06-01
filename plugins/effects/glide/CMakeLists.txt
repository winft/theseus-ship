# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(glide_SOURCES
    glide.cpp
    main.cpp
)

kconfig_add_kcfg_files(glide_SOURCES
    glideconfig.kcfgc
)

kwin_add_builtin_effect(glide ${glide_SOURCES})
target_link_libraries(glide PRIVATE
  KF6::ConfigGui
  kwineffects
)

#######################################
# Config
if (KWIN_BUILD_KCMS)
    set(kwin_glide_config_SRCS glide_config.cpp)
    ki18n_wrap_ui(kwin_glide_config_SRCS glide_config.ui)
    kconfig_add_kcfg_files(kwin_glide_config_SRCS glideconfig.kcfgc)

    kwin_add_effect_config(kwin_glide_config ${kwin_glide_config_SRCS})

    target_link_libraries(kwin_glide_config
      KF6::CoreAddons
    )
endif()
