/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../event_filter.h"

namespace KWin::input
{

/**
 * The remaining default input filter which forwards events to other windows
 */
class forward_filter : public event_filter
{
public:
    bool key(key_event const& event) override;

    bool button(button_event const& event) override;
    bool motion(motion_event const& event) override;
    bool axis(axis_event const& event) override;

    bool touch_down(touch_down_event const& event) override;
    bool touch_motion(touch_motion_event const& event) override;
    bool touch_up(touch_up_event const& event) override;

    bool pinch_begin(pinch_begin_event const& event) override;
    bool pinch_update(pinch_update_event const& event) override;
    bool pinch_end(pinch_end_event const& event) override;

    bool swipe_begin(swipe_begin_event const& event) override;
    bool swipe_update(swipe_update_event const& event) override;
    bool swipe_end(swipe_end_event const& event) override;
};

}
