/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "osd.h"
#include "main.h"
#include "onscreennotification.h"
#include "scripting/platform.h"
#include "workspace.h"

#include <QQmlEngine>

namespace KWin::OSD
{

static OnScreenNotification* create()
{
    auto osd = new OnScreenNotification(workspace());

    osd->setConfig(kwinApp()->config());
    osd->setEngine(workspace()->scripting->qmlEngine());

    return osd;
}

static OnScreenNotification* osd()
{
    static OnScreenNotification* s_osd = create();
    return s_osd;
}

void show(QString const& message, QString const& iconName, int timeout)
{
    if (!kwinApp()->shouldUseWaylandForCompositing()) {
        // FIXME: only supported on Wayland
        return;
    }

    auto notification = osd();
    notification->setIconName(iconName);
    notification->setMessage(message);
    notification->setTimeout(timeout);
    notification->setVisible(true);
}

void show(QString const& message, int timeout)
{
    show(message, QString(), timeout);
}

void show(QString const& message, QString const& iconName)
{
    show(message, iconName, 0);
}

void hide(HideFlags flags)
{
    if (!kwinApp()->shouldUseWaylandForCompositing()) {
        // FIXME: only supported on Wayland
        return;
    }

    osd()->setSkipCloseAnimation(flags.testFlag(HideFlag::SkipCloseAnimation));
    osd()->setVisible(false);
}

}
