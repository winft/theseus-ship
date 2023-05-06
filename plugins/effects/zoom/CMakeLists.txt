# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

set(zoom_SOURCES
    main.cpp
    zoom.cpp
)

if (HAVE_ACCESSIBILITY)
    set(zoom_SOURCES ${zoom_SOURCES}
        accessibilityintegration.cpp
    )
endif()

kconfig_add_kcfg_files(zoom_SOURCES
    zoomconfig.kcfgc
)

kwin_add_builtin_effect(zoom ${zoom_SOURCES})
target_link_libraries(zoom PRIVATE
  KF6::ConfigGui
  KF6::I18n
  KF6::KCMUtils
  kwineffects
  kwinglutils
  base
)

if (HAVE_ACCESSIBILITY)
    target_include_directories(zoom PRIVATE ${QACCESSIBILITYCLIENT_INCLUDE_DIR})
    target_link_libraries(zoom PRIVATE ${QACCESSIBILITYCLIENT_LIBRARY})
endif()

#######################################
# Config
if (KWIN_BUILD_KCMS)
    set(kwin_zoom_config_SRCS zoom_config.cpp)
    ki18n_wrap_ui(kwin_zoom_config_SRCS zoom_config.ui)
    kconfig_add_kcfg_files(kwin_zoom_config_SRCS zoomconfig.kcfgc)

    kwin_add_effect_config(kwin_zoom_config ${kwin_zoom_config_SRCS})

    target_link_libraries(kwin_zoom_config
      KF6::CoreAddons
      KF6::GlobalAccel
      KF6::XmlGui
      base
    )
endif()
