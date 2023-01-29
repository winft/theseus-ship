/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"

#include <KGlobalAccel>
#include <KLocalizedString>
#include <QAction>

namespace KWin::render::post
{

template<typename Input, typename NightColor>
void init_night_color_shortcuts(Input& input, NightColor& manager)
{
    // legacy shortcut with localized key (to avoid breaking existing config)
    if (i18n("Toggle Night Color") != QStringLiteral("Toggle Night Color")) {
        QAction toggleActionLegacy;
        toggleActionLegacy.setProperty("componentName", QStringLiteral(KWIN_NAME));
        toggleActionLegacy.setObjectName(i18n("Toggle Night Color"));
        KGlobalAccel::self()->removeAllShortcuts(&toggleActionLegacy);
    }

    auto toggleAction = new QAction(manager.qobject.get());
    toggleAction->setProperty("componentName", QStringLiteral(KWIN_NAME));
    toggleAction->setObjectName(QStringLiteral("Toggle Night Color"));
    toggleAction->setText(i18n("Toggle Night Color"));

    KGlobalAccel::setGlobalShortcut(toggleAction, QList<QKeySequence>());
    input.registerShortcut(
        QKeySequence(), toggleAction, manager.qobject.get(), [&manager] { manager.toggle(); });
}

}
