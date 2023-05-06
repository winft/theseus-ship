# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

if (KWIN_BUILD_KCMS)
  set(kcm_kwin4_genericscripted_SRCS generic_scripted_config.cpp)
  qt_add_dbus_interface(kcm_kwin4_genericscripted_SRCS ${kwin_effects_dbus_xml} kwineffects_interface)
  add_library(kcm_kwin4_genericscripted MODULE ${kcm_kwin4_genericscripted_SRCS})
  target_link_libraries(kcm_kwin4_genericscripted
    base
    KF6::I18n
    KF6::KCMUtils
    Qt::DBus
    Qt::UiTools
  )
  install(TARGETS kcm_kwin4_genericscripted DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/configs)
endif()
