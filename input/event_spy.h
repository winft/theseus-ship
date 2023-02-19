/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"

#include "utils/algorithm.h"

#include <QSet>

class QTabletEvent;

namespace KWin::input
{

/**
 * Sends an event through all input event spies.
 * The @p function is invoked on each event_spy.
 *
 * The UnaryFunction is defined like the UnaryFunction of std::for_each.
 * The signature of the function should be equivalent to the following:
 * @code
 * void function(event_spy const* spy);
 * @endcode
 *
 * The intended usage is to std::bind the method to invoke on the spies with all arguments
 * bind.
 */
template<typename Spies, typename UnaryFunction>
void process_spies(Spies const& spies, UnaryFunction function)
{
    std::for_each(spies.cbegin(), spies.cend(), function);
}

/**
 * Base class for spying on input events inside InputRedirection.
 *
 * This class is quite similar to InputEventFilter, except that it does not
 * support event filtering. Each event_spy gets to see all input events,
 * the processing happens prior to sending events through the InputEventFilters.
 *
 * Deleting an instance of event_spy automatically uninstalls it from
 * InputRedirection.
 */
template<typename Redirect>
class event_spy
{
public:
    event_spy(Redirect& redirect)
        : redirect{redirect}
    {
    }

    virtual ~event_spy()
    {
        remove_all(redirect.m_spies, this);
    }

    virtual void button(button_event const& /*event*/)
    {
    }

    virtual void motion(motion_event const& /*event*/)
    {
    }

    virtual void axis(axis_event const& /*event*/)
    {
    }

    virtual void key(key_event const& /*event*/)
    {
    }

    virtual void key_repeat(key_event const& /*event*/)
    {
    }

    virtual void touch_down(touch_down_event const& /*event*/)
    {
    }

    virtual void touch_motion(touch_motion_event const& /*event*/)
    {
    }

    virtual void touch_up(touch_up_event const& /*event*/)
    {
    }

    virtual void pinch_begin(pinch_begin_event const& /*event*/)
    {
    }

    virtual void pinch_update(pinch_update_event const& /*event*/)
    {
    }

    virtual void pinch_end(pinch_end_event const& /*event*/)
    {
    }

    virtual void swipe_begin(swipe_begin_event const& /*event*/)
    {
    }

    virtual void swipe_update(swipe_update_event const& /*event*/)
    {
    }

    virtual void swipe_end(swipe_end_event const& /*event*/)
    {
    }

    virtual void hold_begin(hold_begin_event const& /*event*/)
    {
    }

    virtual void hold_end(hold_end_event const& /*event*/)
    {
    }

    virtual void switch_toggle(switch_toggle_event const& /*event*/)
    {
    }

    virtual void tabletToolEvent(QTabletEvent* /*event*/)
    {
    }

    virtual void tabletToolButtonEvent(const QSet<uint>& /*pressedButtons*/)
    {
    }

    virtual void tabletPadButtonEvent(const QSet<uint>& /*pressedButtons*/)
    {
    }

    virtual void tabletPadStripEvent(int /*number*/, int /*position*/, bool /*isFinger*/)
    {
    }

    virtual void tabletPadRingEvent(int /*number*/, int /*position*/, bool /*isFinger*/)
    {
    }

    Redirect& redirect;
};

}
