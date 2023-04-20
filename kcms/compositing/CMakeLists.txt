# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#########################################################################
# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcmkwincompositing\")

################# configure checks and create the configured files #################

include_directories(${KWIN_SOURCE_DIR})

set(kwincompositing_SRC
    main.cpp
    kwincompositingdata.cpp
)

kconfig_add_kcfg_files(kwincompositing_SRC kwincompositing_setting.kcfgc GENERATE_MOC)

qt_add_dbus_interface(kwincompositing_SRC
    ${KWIN_SOURCE_DIR}/render/dbus/org.kde.kwin.Compositing.xml kwin_compositing_interface
)

ki18n_wrap_ui(kwincompositing_SRC compositing.ui)

kcoreaddons_add_plugin(kwincompositing SOURCES ${kwincompositing_SRC} INSTALL_NAMESPACE "plasma/kcms/systemsettings_qwidgets")
kcmutils_generate_desktop_file(kwincompositing)

target_link_libraries(kwincompositing
  KF6::ConfigCore
  KF6::CoreAddons
  KF6::I18n
  KF6::KCMUtils
  KF6::Service
  Qt::DBus
  Qt::Widgets
)
