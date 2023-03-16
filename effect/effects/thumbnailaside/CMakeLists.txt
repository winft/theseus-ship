# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(thumbnailaside_SOURCES
    main.cpp
    thumbnailaside.cpp
)

kconfig_add_kcfg_files(thumbnailaside_SOURCES
    thumbnailasideconfig.kcfgc
)

kwin4_add_effect_module(kwin4_effect_thumbnailaside ${thumbnailaside_SOURCES})
target_link_libraries(kwin4_effect_thumbnailaside PRIVATE
  KF6::ConfigGui
  KF6::I18n
  kwineffects
  kwinglutils
  Qt::Widgets
)

#######################################
# Config
if (KWIN_BUILD_KCMS)
    set(kwin_thumbnailaside_config_SRCS thumbnailaside_config.cpp)
    ki18n_wrap_ui(kwin_thumbnailaside_config_SRCS thumbnailaside_config.ui)
    kconfig_add_kcfg_files(kwin_thumbnailaside_config_SRCS thumbnailasideconfig.kcfgc)

    kwin_add_effect_config(kwin_thumbnailaside_config ${kwin_thumbnailaside_config_SRCS})

    target_link_libraries(kwin_thumbnailaside_config
      KF6::CoreAddons
      KF6::GlobalAccel
      KF6::I18n
      KF6::KCMUtils
      KF6::XmlGui
      KWinEffectsInterface
    )
endif()
