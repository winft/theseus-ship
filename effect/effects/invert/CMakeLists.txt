# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(invert_SOURCES
    invert.cpp
    invert.qrc
    main.cpp
)

kwin4_add_effect_module(kwin4_effect_invert ${invert_SOURCES})
target_link_libraries(kwin4_effect_invert PRIVATE
  KF6::I18n
  kwineffects
  kwinglutils
  Qt::Widgets
)

#######################################
# Config
if (KWIN_BUILD_KCMS)
    set(kwin_invert_config_SRCS invert_config.cpp)

    kwin_add_effect_config(kwin_invert_config ${kwin_invert_config_SRCS})

    target_link_libraries(kwin_invert_config
      KF6::CoreAddons
      KF6::GlobalAccel
      KF6::I18n
      KF6::KCMUtils
      KF6::XmlGui
      KWinEffectsInterface
    )
endif()
