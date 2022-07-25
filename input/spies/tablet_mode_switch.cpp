/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tablet_mode_switch.h"

#include "input/dbus/tablet_mode_manager.h"
#include "input/event.h"
#include "input/switch.h"

namespace KWin::input
{

tablet_mode_switch_spy::tablet_mode_switch_spy(dbus::tablet_mode_manager* manager)
    : manager(manager)
{
}

void tablet_mode_switch_spy::switch_toggle(switch_toggle_event const& event)
{
    if (event.type != switch_type::tablet_mode) {
        return;
    }

    switch (event.state) {
    case input::switch_state::off:
        manager->setIsTablet(false);
        break;
    case input::switch_state::on:
        manager->setIsTablet(true);
        break;
    default:
        Q_UNREACHABLE();
    }
}

}
