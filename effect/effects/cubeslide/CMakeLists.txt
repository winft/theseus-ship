# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect
set(cubeslide_SOURCES
    cubeslide.cpp
    main.cpp
)
kconfig_add_kcfg_files(cubeslide_SOURCES
    cubeslideconfig.kcfgc
)
kwin4_add_effect_module(cubeslide ${cubeslide_SOURCES})
target_link_libraries(cubeslide PRIVATE
  KF6::KCMUtils
  kwineffects
)

#######################################
# Config
if (KWIN_BUILD_KCMS)
  set(kwin_cubeslide_config_SRCS cubeslide_config.cpp)
  ki18n_wrap_ui(kwin_cubeslide_config_SRCS cubeslide_config.ui)
  kconfig_add_kcfg_files(kwin_cubeslide_config_SRCS cubeslideconfig.kcfgc)

  kwin_add_effect_config(kwin_cubeslide_config ${kwin_cubeslide_config_SRCS})

  target_link_libraries(kwin_cubeslide_config
    KF6::CoreAddons
    KF6::I18n
    KF6::KCMUtils
    kwineffects
    KWinEffectsInterface
  )
endif()
