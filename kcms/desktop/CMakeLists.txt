# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# KI18N Translation Domain for this library.
add_definitions(-DTRANSLATION_DOMAIN=\"kcm_kwin_virtualdesktops\")

########### next target ###############

set(kcm_kwin_virtualdesktops_PART_SRCS
    animationsmodel.cpp
    desktopsmodel.cpp
    virtualdesktops.cpp
    virtualdesktopsdata.cpp
)

kconfig_add_kcfg_files(kcm_kwin_virtualdesktops_PART_SRCS virtualdesktopssettings.kcfgc GENERATE_MOC)

kcmutils_add_qml_kcm(kcm_kwin_virtualdesktops SOURCES ${kcm_kwin_virtualdesktops_PART_SRCS})

target_link_libraries(kcm_kwin_virtualdesktops
  kcmkwincommon
  win
  KF6::KCMUtils
  KF6::I18n
  KF6::XmlGui
  Qt::DBus
)

install(FILES virtualdesktopssettings.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})
