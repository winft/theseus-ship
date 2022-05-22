/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"

namespace KWin::input
{

class redirect;

class KWIN_EXPORT screen_edge_filter : public event_filter
{
public:
    explicit screen_edge_filter(input::redirect& redirect);

    bool motion(motion_event const& event) override;
    bool touch_down(touch_down_event const& event) override;
    bool touch_motion(touch_motion_event const& event) override;
    bool touch_up(touch_up_event const& event) override;

private:
    bool m_touchInProgress = false;
    qint32 m_id = 0;
    QPointF m_lastPos;
    input::redirect& redirect;
};

}
