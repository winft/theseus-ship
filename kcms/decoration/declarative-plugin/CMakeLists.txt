# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
ecm_add_qml_module(kdecorationprivatedeclarative URI org.kde.kwin.private.kdecoration DEPENDENCIES QtCore QtQuick GENERATE_PLUGIN_SOURCE)

target_sources(kdecorationprivatedeclarative PRIVATE
    previewbutton.cpp
    previewbridge.cpp
    previewclient.cpp
    previewitem.cpp
    previewsettings.cpp
    buttonsmodel.cpp
    types.h
)

target_link_libraries(kdecorationprivatedeclarative PRIVATE
  como::win
  KDecoration2::KDecoration
  KDecoration2::KDecoration2Private
  KF6::CoreAddons
  KF6::I18n
  KF6::KCMUtils
  KF6::Service
  Qt::DBus
  Qt::Quick
)

ecm_finalize_qml_module(kdecorationprivatedeclarative)
