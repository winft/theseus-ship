# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(wobblywindows_SOURCES
    main.cpp
    wobblywindows.cpp
)

kconfig_add_kcfg_files(wobblywindows_SOURCES
    wobblywindowsconfig.kcfgc
)

kwin_add_builtin_effect(wobblywindows ${wobblywindows_SOURCES})
target_link_libraries(wobblywindows PRIVATE
  KF6::ConfigGui
  render
)

#######################################
# Config
if (KWIN_BUILD_KCMS)
    set(kwin_wobblywindows_config_SRCS wobblywindows_config.cpp)
    ki18n_wrap_ui(kwin_wobblywindows_config_SRCS wobblywindows_config.ui)
    kconfig_add_kcfg_files(kwin_wobblywindows_config_SRCS wobblywindowsconfig.kcfgc)

    kwin_add_effect_config(kwin_wobblywindows_config ${kwin_wobblywindows_config_SRCS})

    target_link_libraries(kwin_wobblywindows_config
      KF6::CoreAddons
    )
endif()
