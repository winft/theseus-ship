#######################################
# Effect
set(flipswitch_SOURCES
    flipswitch.cpp
    main.cpp
)
kconfig_add_kcfg_files(flipswitch_SOURCES
    flipswitchconfig.kcfgc
)
kwin4_add_effect_module(kwin4_effect_flipswitch ${flipswitch_SOURCES})

#######################################
# Config
set(kwin_flipswitch_config_SRCS flipswitch_config.cpp)
ki18n_wrap_ui(kwin_flipswitch_config_SRCS flipswitch_config.ui)
kconfig_add_kcfg_files(kwin_flipswitch_config_SRCS flipswitchconfig.kcfgc)

add_library(kwin_flipswitch_config MODULE ${kwin_flipswitch_config_SRCS})

target_link_libraries(kwin_flipswitch_config
    KF5::ConfigWidgets
    KF5::GlobalAccel
    KF5::I18n
    KF5::XmlGui
    KWinEffectsInterface
)

kcoreaddons_desktop_to_json(kwin_flipswitch_config flipswitch_config.desktop SERVICE_TYPES kcmodule.desktop)

install(
    TARGETS
        kwin_flipswitch_config
    DESTINATION
        ${PLUGIN_INSTALL_DIR}/kwin/effects/configs
)
