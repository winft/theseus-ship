# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(plugin_SRCS
    previewbutton.cpp
    previewbridge.cpp
    previewclient.cpp
    previewitem.cpp
    previewsettings.cpp
    plugin.cpp
    buttonsmodel.cpp
    ../../../lib/win/deco/palette.cpp
    ../../../lib/win/deco/decorations_logging.cpp
)

add_library(kdecorationprivatedeclarative SHARED ${plugin_SRCS})
target_link_libraries(kdecorationprivatedeclarative
  KDecoration2::KDecoration
  KDecoration2::KDecoration2Private
  KF6::CoreAddons
  KF6::I18n
  KF6::KCMUtils
  KF6::Service
  Qt::DBus
  Qt::Quick
)

install(TARGETS kdecorationprivatedeclarative DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/kwin/private/kdecoration )
install(FILES qmldir DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/kwin/private/kdecoration )
