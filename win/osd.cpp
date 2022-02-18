/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "osd.h"

#include "osd_notification.h"
#include "space.h"

#include "main.h"
#include "scripting/platform.h"

#include <QQmlEngine>

namespace KWin::win
{

static osd_notification* create()
{
    auto osd = new osd_notification(workspace());

    osd->setConfig(kwinApp()->config());
    osd->setEngine(workspace()->scripting->qmlEngine());

    return osd;
}

static osd_notification* osd()
{
    static osd_notification* s_osd = create();
    return s_osd;
}

void osd_show(QString const& message, QString const& iconName, int timeout)
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

void osd_show(QString const& message, int timeout)
{
    osd_show(message, QString(), timeout);
}

void osd_show(QString const& message, QString const& iconName)
{
    osd_show(message, iconName, 0);
}

void osd_hide(osd_hide_flags hide_flags)
{
    if (!kwinApp()->shouldUseWaylandForCompositing()) {
        // FIXME: only supported on Wayland
        return;
    }

    osd()->setSkipCloseAnimation(flags(hide_flags & osd_hide_flags::skip_close_animation));
    osd()->setVisible(false);
}

}
