# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(lookingglass_SOURCES
    lookingglass.cpp
    lookingglass.qrc
    main.cpp
)

kconfig_add_kcfg_files(lookingglass_SOURCES
    lookingglassconfig.kcfgc
)

kwin_add_builtin_effect(lookingglass ${lookingglass_SOURCES})
target_link_libraries(lookingglass PRIVATE
  KF6::ConfigGui
  KF6::I18n
  KF6::KCMUtils
  kwineffects
  kwinglutils
)

#######################################
# Config
if (KWIN_BUILD_KCMS)
    set(kwin_lookingglass_config_SRCS lookingglass_config.cpp)
    ki18n_wrap_ui(kwin_lookingglass_config_SRCS lookingglass_config.ui)
    kconfig_add_kcfg_files(kwin_lookingglass_config_SRCS lookingglassconfig.kcfgc)

    kwin_add_effect_config(kwin_lookingglass_config ${kwin_lookingglass_config_SRCS})

    target_link_libraries(kwin_lookingglass_config
      KF6::CoreAddons
      KF6::GlobalAccel
      KF6::XmlGui
    )
endif()
