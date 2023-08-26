# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect
set(cube_SOURCES
    cube.cpp
    cube.qrc
    main.cpp
)
kconfig_add_kcfg_files(cube_SOURCES
    cubeconfig.kcfgc
)
kwin_add_builtin_effect(cube ${cube_SOURCES})
target_link_libraries(cube PRIVATE
  KF6::I18n
  KF6::KCMUtils
  Qt::Concurrent
  render
)

# Data files
install(FILES data/cubecap.png DESTINATION ${KDE_INSTALL_DATADIR}/kwin)

#######################################
# Config
if (KWIN_BUILD_KCMS)
  set(kwin_cube_config_SRCS cube_config.cpp)
  ki18n_wrap_ui(kwin_cube_config_SRCS cube_config.ui)
  kconfig_add_kcfg_files(kwin_cube_config_SRCS cubeconfig.kcfgc)

  kwin_add_effect_config(kwin_cube_config ${kwin_cube_config_SRCS})

  target_link_libraries(kwin_cube_config
    KF6::GlobalAccel
    KF6::XmlGui
    KF6::KIOWidgets
  )
endif()
