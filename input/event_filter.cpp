/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event_filter.h"

#include "redirect.h"

#include "main.h"

namespace KWin::input
{

event_filter::event_filter() = default;

event_filter::~event_filter()
{
    assert(kwinApp()->input->redirect);
    kwinApp()->input->redirect->uninstallInputEventFilter(this);
}

bool event_filter::button([[maybe_unused]] button_event const& event)
{
    return false;
}

bool event_filter::motion([[maybe_unused]] motion_event const& event)
{
    return false;
}

bool event_filter::axis([[maybe_unused]] axis_event const& event)
{
    return false;
}

bool event_filter::key(key_event const& /*event*/)
{
    return false;
}

bool event_filter::key_repeat(key_event const& /*event*/)
{
    return false;
}

bool event_filter::touch_down(touch_down_event const& /*event*/)
{
    return false;
}

bool event_filter::touch_motion(touch_motion_event const& /*event*/)
{
    return false;
}

bool event_filter::touch_up(touch_up_event const& /*event*/)
{
    return false;
}

bool event_filter::pinch_begin(pinch_begin_event const& /*event*/)
{
    return false;
}

bool event_filter::pinch_update(pinch_update_event const& /*event*/)
{
    return false;
}

bool event_filter::pinch_end(pinch_end_event const& /*event*/)
{
    return false;
}

bool event_filter::swipe_begin(swipe_begin_event const& /*event*/)
{
    return false;
}

bool event_filter::swipe_update(swipe_update_event const& /*event*/)
{
    return false;
}

bool event_filter::swipe_end(swipe_end_event const& /*event*/)
{
    return false;
}

bool event_filter::switch_toggle(switch_toggle_event const& /*event*/)
{
    return false;
}

bool event_filter::tabletToolEvent(QTabletEvent* event)
{
    Q_UNUSED(event)
    return false;
}

bool event_filter::tabletToolButtonEvent(const QSet<uint>& pressedButtons)
{
    Q_UNUSED(pressedButtons)
    return false;
}

bool event_filter::tabletPadButtonEvent(const QSet<uint>& pressedButtons)
{
    Q_UNUSED(pressedButtons)
    return false;
}

bool event_filter::tabletPadStripEvent(int number, int position, bool isFinger)
{
    Q_UNUSED(number)
    Q_UNUSED(position)
    Q_UNUSED(isFinger)
    return false;
}

bool event_filter::tabletPadRingEvent(int number, int position, bool isFinger)
{
    Q_UNUSED(number)
    Q_UNUSED(position)
    Q_UNUSED(isFinger)
    return false;
}

}
