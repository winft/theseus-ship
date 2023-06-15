# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

# KI18N Translation Domain for this library
add_definitions(-DTRANSLATION_DOMAIN=\"kcmkwincommon\")

set(kcmkwincommon_SRC
    effectsmodel.cpp
)

qt_add_dbus_interface(kcmkwincommon_SRC
    ${KWIN_SOURCE_DIR}/lib/effect/org.kde.kwin.Effects.xml kwin_effects_interface
)

add_library(kcmkwincommon SHARED ${kcmkwincommon_SRC})

target_link_libraries(kcmkwincommon
  base
  KF6::ConfigCore
  KF6::CoreAddons
  KF6::I18n
  KF6::KCMUtils
  KF6::Package
  Qt::Core
  Qt::DBus
)

set_target_properties(kcmkwincommon PROPERTIES
    VERSION ${CMAKE_PROJECT_VERSION}
    SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
)

install(TARGETS kcmkwincommon ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)

set(kcm_kwin4_genericscripted_SRCS generic_scripted_config.cpp)
qt_add_dbus_interface(kcm_kwin4_genericscripted_SRCS
  ${KWIN_SOURCE_DIR}/lib/effect/org.kde.kwin.Effects.xml kwin_effects_interface
)
add_library(kcm_kwin4_genericscripted MODULE ${kcm_kwin4_genericscripted_SRCS})
target_link_libraries(kcm_kwin4_genericscripted
  base
  KF6::I18n
  KF6::KCMUtils
  Qt::DBus
  Qt::UiTools
)
install(TARGETS kcm_kwin4_genericscripted DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/configs)
