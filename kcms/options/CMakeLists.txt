# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

########### next target ###############
# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcmkwm\")

include_directories(${KWIN_SOURCE_DIR})

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
  ${KWIN_SOURCE_DIR}/render/dbus/org.kde.kwin.Effects.xml
  kwin_effects_interface
)

kcoreaddons_add_plugin(kcm_kwinoptions SOURCES ${kcm_kwinoptions_PART_SRCS} INSTALL_NAMESPACE "plasma/kcms/systemsettings_qwidgets")
kcmutils_generate_desktop_file(kcm_kwinoptions)
target_link_libraries(kcm_kwinoptions
  kwin
  KF5::ConfigWidgets
  Qt::DBus
  KF5::I18n
  KF5::KCMUtils
  KF5::Service
  KF5::WindowSystem
)
