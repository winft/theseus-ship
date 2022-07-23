/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"

#include "main.h"
#include "redirect.h"

#include <QSet>
#include <QTabletEvent>

namespace KWin::input
{

/**
 * Base class for filtering input events inside InputRedirection.
 *
 * The idea behind the event_filter is to have task oriented
 * filters. E.g. there is one filter taking care of a locked screen,
 * one to take care of interacting with window decorations, etc.
 *
 * A concrete subclass can reimplement the virtual methods and decide
 * whether an event should be filtered out or not by returning either
 * @c true or @c false. E.g. the lock screen filter can easily ensure
 * that all events are filtered out.
 *
 * As soon as a filter returns @c true the processing is stopped. If
 * a filter returns @c false the next one is invoked. This means a filter
 * installed early gets to see more events than a filter installed later on.
 *
 * Deleting an instance of event_filter automatically uninstalls it from
 * InputRedirection.
 */
class event_filter
{
public:
    event_filter() = default;

    virtual ~event_filter()
    {
        assert(kwinApp()->input->redirect);
        kwinApp()->input->redirect->uninstallInputEventFilter(this);
    }

    virtual bool button(button_event const& /*event*/)
    {
        return false;
    }

    virtual bool motion(motion_event const& /*event*/)
    {
        return false;
    }
    virtual bool axis(axis_event const& /*event*/)
    {
        return false;
    }

    virtual bool key(key_event const& /*event*/)
    {
        return false;
    }

    virtual bool key_repeat(key_event const& /*event*/)
    {
        return false;
    }

    virtual bool touch_down(touch_down_event const& /*event*/)
    {
        return false;
    }

    virtual bool touch_motion(touch_motion_event const& /*event*/)
    {
        return false;
    }

    virtual bool touch_up(touch_up_event const& /*event*/)
    {
        return false;
    }

    virtual bool pinch_begin(pinch_begin_event const& /*event*/)
    {
        return false;
    }

    virtual bool pinch_update(pinch_update_event const& /*event*/)
    {
        return false;
    }

    virtual bool pinch_end(pinch_end_event const& /*event*/)
    {
        return false;
    }

    virtual bool swipe_begin(swipe_begin_event const& /*event*/)
    {
        return false;
    }

    virtual bool swipe_update(swipe_update_event const& /*event*/)
    {
        return false;
    }

    virtual bool swipe_end(swipe_end_event const& /*event*/)
    {
        return false;
    }

    virtual bool switch_toggle(switch_toggle_event const& /*event*/)
    {
        return false;
    }

    virtual bool tabletToolEvent(QTabletEvent* /*event*/)
    {
        return false;
    }

    virtual bool tabletToolButtonEvent(const QSet<uint>& /*buttons*/)
    {
        return false;
    }

    virtual bool tabletPadButtonEvent(const QSet<uint>& /*buttons*/)
    {
        return false;
    }

    virtual bool tabletPadStripEvent(int /*number*/, int /*position*/, bool /*isFinger*/)
    {
        return false;
    }

    virtual bool tabletPadRingEvent(int /*number*/, int /*position*/, bool /*isFinger*/)
    {
        return false;
    }
};

}
