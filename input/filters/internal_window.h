/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"

namespace KWin::input
{

class redirect;

class KWIN_EXPORT internal_window_filter : public event_filter
{
public:
    explicit internal_window_filter(input::redirect& redirect);

    bool key(key_event const& event) override;
    bool key_repeat(key_event const& event) override;

    bool button(button_event const& event) override;
    bool motion(motion_event const& event) override;
    bool axis(axis_event const& event) override;

    bool touch_down(touch_down_event const& event) override;
    bool touch_motion(touch_motion_event const& event) override;
    bool touch_up(touch_up_event const& event) override;

private:
    QSet<qint32> m_pressedIds;
    QPointF m_lastGlobalTouchPos;
    QPointF m_lastLocalTouchPos;
    input::redirect& redirect;
};

}
