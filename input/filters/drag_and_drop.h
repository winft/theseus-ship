/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"

namespace KWin::input
{

class redirect;

class drag_and_drop_filter : public event_filter
{
public:
    explicit drag_and_drop_filter(input::redirect& redirect);

    bool button(button_event const& event) override;
    bool motion(motion_event const& event) override;

    bool touch_down(touch_down_event const& event) override;
    bool touch_motion(touch_motion_event const& event) override;
    bool touch_up(touch_up_event const& event) override;

private:
    qint32 m_touchId = -1;
    input::redirect& redirect;
};

}
