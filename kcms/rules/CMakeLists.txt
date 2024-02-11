# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcm_kwinrules\")

set(kwinrules_SRCS
    optionsmodel.cpp
    ruleitem.cpp
    rulesmodel.cpp
    rulebookmodel.cpp
)

# kconfig_add_kcfg_files(kwinrules_SRCS ../../lib/win/rules/kconfig/rules_settings.kcfgc)
# kconfig_add_kcfg_files(kwinrules_SRCS ../../lib/win/rules/kconfig/rules_book_settings_base.kcfgc)

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
  como::input
  como::win-x11
  KF6::KCMUtils
  KF6::WindowSystem
)

target_link_libraries(KWinRulesObjects ${kcm_libs} ${kwin_kcm_rules_XCB_LIBS})

kcmutils_add_qml_kcm(kcm_kwinrules SOURCES kcmrules.cpp)
target_link_libraries(kcm_kwinrules KWinRulesObjects)
