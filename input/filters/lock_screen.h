/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"

namespace KWin::input
{

class redirect;

class lock_screen_filter : public event_filter
{
public:
    explicit lock_screen_filter(input::redirect& redirect);

    bool button(button_event const& event) override;
    bool motion(motion_event const& event) override;
    bool axis(axis_event const& event) override;

    bool key(key_event const& event) override;
    bool key_repeat(key_event const& event) override;

    bool touch_down(touch_down_event const& event) override;
    bool touch_motion(touch_motion_event const& event) override;
    bool touch_up(touch_up_event const& event) override;

    bool pinch_begin(pinch_begin_event const& event) override;
    bool pinch_update(pinch_update_event const& event) override;
    bool pinch_end(pinch_end_event const& event) override;

    bool swipe_begin(swipe_begin_event const& event) override;
    bool swipe_update(swipe_update_event const& event) override;
    bool swipe_end(swipe_end_event const&) override;

private:
    bool pointerSurfaceAllowed() const;
    bool keyboardSurfaceAllowed() const;
    bool touchSurfaceAllowed() const;

    input::redirect& redirect;
};

}
