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
    ../../../win/deco/palette.cpp
    ../../../win/deco/decorations_logging.cpp
)

add_library(kdecorationprivatedeclarative SHARED ${plugin_SRCS})
target_link_libraries(kdecorationprivatedeclarative
    KDecoration2::KDecoration
    KDecoration2::KDecoration2Private
    Qt::DBus
    Qt::Quick
    KF5::CoreAddons
    KF5::ConfigWidgets
    KF5::I18n
    KF5::Service
)

install(TARGETS kdecorationprivatedeclarative DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/kwin/private/kdecoration )
install(FILES qmldir DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/kwin/private/kdecoration )
