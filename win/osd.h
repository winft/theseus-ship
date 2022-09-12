/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "osd_notification.h"

#include "main.h"
#include "utils/flags.h"

#include <QString>

namespace KWin::win
{

enum class osd_hide_flags {
    none = 0x0,
    skip_close_animation = 0x1,
};

}

ENUM_FLAGS(KWin::win::osd_hide_flags)

namespace KWin::win
{

template<typename Space>
static void create_osd(Space& space)
{
    assert(!space.osd);
    space.osd = std::make_unique<osd_notification<typename Space::input_t>>(*space.input);

    space.osd->m_config = kwinApp()->config();
    space.osd->m_qmlEngine = space.scripting->qml_engine;
}

template<typename Space>
void osd_show(Space& space, QString const& message, QString const& iconName, int timeout)
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

template<typename Space>
auto get_osd(Space& space) -> osd_notification<typename Space::input_t>*
{
    if (!space.osd) {
        create_osd(space);
    }
    return space.osd.get();
}

template<typename Space>
void osd_show(Space& space, QString const& message, QString const& iconName = QString())
{
    osd_show(space, message, iconName, 0);
}

template<typename Space>
void osd_show(Space& space, QString const& message, int timeout)
{
    osd_show(space, message, QString(), timeout);
}

template<typename Space>
void osd_hide(Space& space, osd_hide_flags hide_flags = osd_hide_flags::none)
{
    if (!kwinApp()->shouldUseWaylandForCompositing()) {
        // FIXME: only supported on Wayland
        return;
    }

    get_osd(space)->setSkipCloseAnimation(flags(hide_flags & osd_hide_flags::skip_close_animation));
    get_osd(space)->qobject->setVisible(false);
}

}
