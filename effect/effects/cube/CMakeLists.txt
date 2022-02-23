#######################################
# Effect
set(cube_SOURCES
    cube.cpp
    cube_proxy.cpp
    cube.qrc
    main.cpp
)
kconfig_add_kcfg_files(cube_SOURCES
    cubeconfig.kcfgc
)
kwin4_add_effect_module(kwin4_effect_cube ${cube_SOURCES})
target_link_libraries(kwin4_effect_cube PRIVATE
    kwineffects
    KF5::ConfigWidgets
    KF5::GlobalAccel
    KF5::I18n
    Qt::Concurrent
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
    KF5::ConfigWidgets
    KF5::CoreAddons
    KF5::GlobalAccel
    KF5::I18n
    KF5::XmlGui
    KF5::KIOWidgets
    KWinEffectsInterface
  )
endif()
