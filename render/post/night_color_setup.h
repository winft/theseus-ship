/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"

#include <KLocalizedString>
#include <QAction>

namespace KWin::render::post
{

template<typename Input, typename NightColor>
void init_night_color_shortcuts(Input& input, NightColor& manager)
{
    auto toggleAction = new QAction(manager.qobject.get());
    toggleAction->setProperty("componentName", QStringLiteral(KWIN_NAME));
    toggleAction->setObjectName(QStringLiteral("Toggle Night Color"));
    toggleAction->setText(i18n("Toggle Night Color"));

    input.shortcuts->register_keyboard_default_shortcut(toggleAction, {});
    input.shortcuts->register_keyboard_shortcut(
        toggleAction, {}, input::shortcut_loading::global_lookup);
    input.registerShortcut(
        QKeySequence(), toggleAction, manager.qobject.get(), [&manager] { manager.toggle(); });
}

}
