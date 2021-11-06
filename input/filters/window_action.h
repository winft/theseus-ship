/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../event_filter.h"

namespace KWin
{
class Toplevel;

namespace input
{

/**
 * This filter implements window actions. If the event should not be passed to the
 * current pointer window it will filter out the event
 */
class window_action_filter : public event_filter
{
public:
    bool button(button_event const& event) override;
    bool touch_down(touch_down_event const& event) override;
    bool axis(axis_event const& event) override;

private:
    Toplevel* get_focus_lead(Toplevel* focus);
};

}
}
