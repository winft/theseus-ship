#######################################
# Effect
set(cubeslide_SOURCES
    cubeslide.cpp
    main.cpp
)
kconfig_add_kcfg_files(cubeslide_SOURCES
    cubeslideconfig.kcfgc
)
kwin4_add_effect_module(kwin4_effect_cubeslide ${cubeslide_SOURCES})

#######################################
# Config

set(kwin_cubeslide_config_SRCS cubeslide_config.cpp)
ki18n_wrap_ui(kwin_cubeslide_config_SRCS cubeslide_config.ui)
kconfig_add_kcfg_files(kwin_cubeslide_config_SRCS cubeslideconfig.kcfgc)

kwin_add_effect_config(kwin_cubeslide_config ${kwin_cubeslide_config_SRCS})

target_link_libraries(kwin_cubeslide_config
    KF5::ConfigWidgets
    KF5::I18n
    kwineffects
    KWinEffectsInterface
)
