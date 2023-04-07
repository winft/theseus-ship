# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcm_kwinrules\")

include_directories(../../)

set(kwinrules_SRCS
    ../../base/logging.cpp
    ../../win/rules/book_settings.cpp
    ../../input/cursor.cpp
    ../../input/cursor_shape.cpp
    ../../input/singleton_interface.cpp
    ../../input/x11/cursor.cpp
    ../../win/rules/ruling.cpp
    ../../win/dbus/virtual_desktop_types.cpp
    optionsmodel.cpp
    ruleitem.cpp
    rulesmodel.cpp
    rulebookmodel.cpp
)

kconfig_add_kcfg_files(kwinrules_SRCS ../../win/rules/kconfig/rules_settings.kcfgc)
kconfig_add_kcfg_files(kwinrules_SRCS ../../win/rules/kconfig/rules_book_settings_base.kcfgc)

add_library(KWinRulesObjects STATIC ${kwinrules_SRCS})
set_target_properties(KWinRulesObjects PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

set(kwin_kcm_rules_XCB_LIBS
    XCB::CURSOR
    XCB::XCB
    XCB::XFIXES
)

set(kcm_libs
  KF6::KCMUtils
  KF6::ConfigWidgets
  KF6::QuickAddons
  KF6::WindowSystem
  kwin
  kwin_x11_lib
)

target_link_libraries(KWinRulesObjects ${kcm_libs} ${kwin_kcm_rules_XCB_LIBS})

add_executable(kwin_rules_dialog main.cpp)
target_link_libraries(kwin_rules_dialog KWinRulesObjects)
target_compile_definitions(kwin_rules_dialog PRIVATE KCMRULES_PLUGIN="plasma/kcms/systemsettings/kcm_kwinrules")
install(TARGETS kwin_rules_dialog DESTINATION ${KDE_INSTALL_LIBEXECDIR})

kcoreaddons_add_plugin(kcm_kwinrules SOURCES kcmrules.cpp INSTALL_NAMESPACE "plasma/kcms/systemsettings")
kcmutils_generate_desktop_file(kcm_kwinrules)
target_link_libraries(kcm_kwinrules KWinRulesObjects)

kpackage_install_package(package kcm_kwinrules kcms)

install(FILES org.kde.kwin_rules_dialog.desktop DESTINATION ${KDE_INSTALL_APPDIR})
