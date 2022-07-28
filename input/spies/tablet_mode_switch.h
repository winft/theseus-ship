/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event.h"
#include "input/event_spy.h"
#include "input/switch.h"

namespace KWin::input
{

template<typename Manager>
class tablet_mode_switch_spy : public input::event_spy
{
public:
    tablet_mode_switch_spy(input::redirect& redirect, Manager& manager)
        : event_spy(redirect)
        , manager(manager)
    {
    }

    void switch_toggle(switch_toggle_event const& event) override
    {
        if (event.type != switch_type::tablet_mode) {
            return;
        }

        switch (event.state) {
        case input::switch_state::off:
            manager.setIsTablet(false);
            break;
        case input::switch_state::on:
            manager.setIsTablet(true);
            break;
        default:
            Q_UNREACHABLE();
        }
    }

private:
    Manager& manager;
};

}
