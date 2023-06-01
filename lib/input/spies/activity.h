/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_spy.h"

namespace KWin::input
{

template<typename Redirect>
class activity_spy : public event_spy<Redirect>
{
public:
    explicit activity_spy(Redirect& redirect)
        : event_spy<Redirect>(redirect)
    {
    }

    void button(button_event const& /*event*/) override
    {
        report_activity();
    }

    void motion(motion_event const& /*event*/) override
    {
        report_activity();
    }

    void axis(axis_event const& /*event*/) override
    {
        report_activity();
    }

    void key(key_event const& /*event*/) override
    {
        report_activity();
    }

    void key_repeat(key_event const& /*event*/) override
    {
        report_activity();
    }

    void touch_down(touch_down_event const& /*event*/) override
    {
        report_activity();
    }

    void touch_motion(touch_motion_event const& /*event*/) override
    {
        report_activity();
    }

    void touch_up(touch_up_event const& /*event*/) override
    {
        report_activity();
    }

    void pinch_begin(pinch_begin_event const& /*event*/) override
    {
        report_activity();
    }

    void pinch_update(pinch_update_event const& /*event*/) override
    {
        report_activity();
    }

    void pinch_end(pinch_end_event const& /*event*/) override
    {
        report_activity();
    }

    void swipe_begin(swipe_begin_event const& /*event*/) override
    {
        report_activity();
    }

    void swipe_update(swipe_update_event const& /*event*/) override
    {
        report_activity();
    }

    void swipe_end(swipe_end_event const& /*event*/) override
    {
        report_activity();
    }

    void switch_toggle(switch_toggle_event const& /*event*/) override
    {
        report_activity();
    }

    void tabletToolEvent(QTabletEvent* /*event*/) override
    {
        report_activity();
    }

    void tabletToolButtonEvent(const QSet<uint>& /*pressedButtons*/) override
    {
        report_activity();
    }

    void tabletPadButtonEvent(const QSet<uint>& /*pressedButtons*/) override
    {
        report_activity();
    }

    void tabletPadStripEvent(int /*number*/, int /*position*/, bool /*isFinger*/) override
    {
        report_activity();
    }

    void tabletPadRingEvent(int /*number*/, int /*position*/, bool /*isFinger*/) override
    {
        report_activity();
    }

private:
    void report_activity()
    {
        this->redirect.platform.idle.report_activity();
    }
};

}
