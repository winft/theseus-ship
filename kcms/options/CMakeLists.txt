# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

########### next target ###############
# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcmkwm\")

set(kcm_kwinoptions_PART_SRCS
    main.cpp
    mouse.cpp
    windows.cpp
)

ki18n_wrap_ui(kcm_kwinoptions_PART_SRCS
    actions.ui
    advanced.ui
    focus.ui
    mouse.ui
    moving.ui
)

kcmutils_generate_module_data(
    kcm_kwinoptions_PART_SRCS
    MODULE_DATA_HEADER kwinoptionsdata.h
    MODULE_DATA_CLASS_NAME KWinOptionsData
    SETTINGS_HEADERS kwinoptions_settings.h kwinoptions_kdeglobals_settings.h
    SETTINGS_CLASSES KWinOptionsSettings KWinOptionsKDEGlobalsSettings
)

kconfig_add_kcfg_files(kcm_kwinoptions_PART_SRCS kwinoptions_settings.kcfgc GENERATE_MOC)
kconfig_add_kcfg_files(kcm_kwinoptions_PART_SRCS kwinoptions_kdeglobals_settings.kcfgc GENERATE_MOC)

qt_add_dbus_interface(kcm_kwinoptions_PART_SRCS
  ${KWIN_EFFECTS_INTERFACE} kwin_effects_interface
)

kcoreaddons_add_plugin(kcm_kwinoptions SOURCES ${kcm_kwinoptions_PART_SRCS} INSTALL_NAMESPACE "plasma/kcms/systemsettings_qwidgets")
kcmutils_generate_desktop_file(kcm_kwinoptions)
target_link_libraries(kcm_kwinoptions
  KF6::I18n
  KF6::KCMUtils
  KF6::WindowSystem
  Qt::DBus
  como::win
)
