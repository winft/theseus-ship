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

static void create_osd(win::space& space)
{
    assert(!space.osd);
    space.osd = std::make_unique<osd_notification<input::redirect>>(space.input.get());

    space.osd->m_config = kwinApp()->config();
    space.osd->m_qmlEngine = space.scripting->qml_engine;
}

static osd_notification<input::redirect>* get_osd(win::space& space)
{
    if (!space.osd) {
        create_osd(space);
    }
    return space.osd.get();
}

void osd_show(win::space& space, QString const& message, QString const& iconName, int timeout)
{
    if (!kwinApp()->shouldUseWaylandForCompositing()) {
        // FIXME: only supported on Wayland
        return;
    }

    auto notification = get_osd(space);
    notification->qobject->setIconName(iconName);
    notification->qobject->setMessage(message);
    notification->qobject->setTimeout(timeout);
    notification->qobject->setVisible(true);
}

void osd_show(win::space& space, QString const& message, int timeout)
{
    osd_show(space, message, QString(), timeout);
}

void osd_show(win::space& space, QString const& message, QString const& iconName)
{
    osd_show(space, message, iconName, 0);
}

void osd_hide(win::space& space, osd_hide_flags hide_flags)
{
    if (!kwinApp()->shouldUseWaylandForCompositing()) {
        // FIXME: only supported on Wayland
        return;
    }

    get_osd(space)->setSkipCloseAnimation(flags(hide_flags & osd_hide_flags::skip_close_animation));
    get_osd(space)->qobject->setVisible(false);
}

}
