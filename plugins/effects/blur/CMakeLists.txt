# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(blur_SOURCES
    blur.cpp
    blur.qrc
    blurshader.cpp
    main.cpp
)

kconfig_add_kcfg_files(blur_SOURCES
    blurconfig.kcfgc
)

kwin_add_builtin_effect(blur ${blur_SOURCES})
target_link_libraries(blur PRIVATE
  KDecoration2::KDecoration
  KF6::ConfigGui
  render
)

#######################################
# Config
if (KWIN_BUILD_KCMS)
    set(kwin_blur_config_SRCS blur_config.cpp)
    ki18n_wrap_ui(kwin_blur_config_SRCS blur_config.ui)
    kconfig_add_kcfg_files(kwin_blur_config_SRCS blurconfig.kcfgc)

    kwin_add_effect_config(kwin_blur_config ${kwin_blur_config_SRCS})

    target_link_libraries(kwin_blur_config
      KF6::CoreAddons
    )
endif()
