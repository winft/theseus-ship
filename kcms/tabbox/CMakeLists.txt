# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcm_kwintabbox\")

########### next target ###############

set(kcm_kwintabbox_PART_SRCS
    layoutpreview.cpp
    main.cpp
    thumbnailitem.cpp
    kwintabboxconfigform.cpp
    kwintabboxdata.cpp
    shortcutsettings.cpp
)

ki18n_wrap_ui(kcm_kwintabbox_PART_SRCS main.ui)
qt_add_dbus_interface(kcm_kwintabbox_PART_SRCS
  ${KWIN_EFFECTS_INTERFACE} kwin_effects_interface
)

kconfig_add_kcfg_files(kcm_kwintabbox_PART_SRCS kwintabboxsettings.kcfgc kwinswitcheffectsettings.kcfgc kwinpluginssettings.kcfgc)
kcoreaddons_add_plugin(kcm_kwintabbox SOURCES ${kcm_kwintabbox_PART_SRCS} INSTALL_NAMESPACE "plasma/kcms/systemsettings_qwidgets")

kcmutils_generate_desktop_file(kcm_kwintabbox)
target_link_libraries(kcm_kwintabbox
  como::win
  KF6::GlobalAccel
  KF6::I18n
  KF6::KCMUtils
  KF6::NewStuffWidgets
  KF6::Package
  KF6::Service
  KF6::XmlGui
  Qt::Quick
  XCB::XCB
)

########### install files ###############
install(FILES thumbnails/falkon.png
              thumbnails/kmail.png
              thumbnails/systemsettings.png
              thumbnails/dolphin.png
              thumbnails/desktop.png
        DESTINATION ${KDE_INSTALL_DATADIR}/kwin/kcm_kwintabbox)
install(FILES kwinswitcher.knsrc DESTINATION ${KDE_INSTALL_KNSRCDIR})
