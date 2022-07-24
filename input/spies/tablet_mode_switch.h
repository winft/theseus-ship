/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/dbus/tablet_mode_manager.h"
#include "input/event.h"
#include "input/event_spy.h"
#include "input/switch.h"

namespace KWin::input
{

class tablet_mode_switch_spy : public input::event_spy
{
public:
    explicit tablet_mode_switch_spy(dbus::tablet_mode_manager* manager)
        : manager(manager)
    {
    }

    void switch_toggle(switch_toggle_event const& event) override
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

private:
    dbus::tablet_mode_manager* const manager;
};

}
