# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcm_kwindecoration\")

add_subdirectory(declarative-plugin)

set(kcmkwindecoration_SRCS
    declarative-plugin/buttonsmodel.cpp
    decorationmodel.cpp
    kcm.cpp
    utils.cpp
)

kcmutils_generate_module_data(
    kcmkwindecoration_SRCS
    MODULE_DATA_HEADER kwindecorationdata.h
    MODULE_DATA_CLASS_NAME KWinDecorationData
    SETTINGS_HEADERS kwindecorationsettings.h
    SETTINGS_CLASSES KWinDecorationSettings
)

kconfig_add_kcfg_files(kcmkwindecoration_SRCS kwindecorationsettings.kcfgc GENERATE_MOC)

kcmutils_add_qml_kcm(kcm_kwindecoration SOURCES ${kcmkwindecoration_SRCS})

target_link_libraries(kcm_kwindecoration
  KDecoration2::KDecoration
  KF6::I18n
  KF6::KCMUtils
  Qt::DBus
  Qt::Quick
  como::base
)

set(kwin-applywindowdecoration_SRCS
    kwin-applywindowdecoration.cpp
    decorationmodel.cpp
    utils.cpp
)

kconfig_add_kcfg_files(kwin-applywindowdecoration_SRCS kwindecorationsettings.kcfgc GENERATE_MOC)
add_executable(kwin-applywindowdecoration ${kwin-applywindowdecoration_SRCS})

target_link_libraries(kwin-applywindowdecoration
  KDecoration2::KDecoration
  KF6::I18n
  KF6::KCMUtils
  Qt::DBus
  como::base
)

configure_file(window-decorations.knsrc.cmake ${CMAKE_CURRENT_BINARY_DIR}/window-decorations.knsrc)

install(FILES kwindecorationsettings.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/window-decorations.knsrc DESTINATION ${KDE_INSTALL_KNSRCDIR})
install(TARGETS kwin-applywindowdecoration DESTINATION ${KDE_INSTALL_LIBEXECDIR})
