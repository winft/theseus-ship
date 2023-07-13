# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcm_kwin_scripts\")

kcmutils_add_qml_kcm(kcm_kwin_scripts INSTALL_NAMESPACE plasma/kcms/systemsettings)

target_sources(kcm_kwin_scripts PRIVATE
    module.cpp
    kwinscriptsdata.cpp
)

target_link_libraries(kcm_kwin_scripts
  KF6::I18n
  KF6::KCMUtils
  KF6::KCMUtilsCore
  KF6::Package
  Qt::DBus
  base
)

install(FILES kwinscripts.knsrc DESTINATION ${KDE_INSTALL_KNSRCDIR})
