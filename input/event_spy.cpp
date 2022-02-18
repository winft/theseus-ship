/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event_spy.h"

#include "redirect.h"

#include "main.h"

#include <QPointF>
#include <QSizeF>

namespace KWin::input
{

event_spy::event_spy() = default;

event_spy::~event_spy()
{
    assert(kwinApp()->input->redirect);
    kwinApp()->input->redirect->uninstallInputEventSpy(this);
}

void event_spy::button([[maybe_unused]] button_event const& event)
{
}

void event_spy::motion([[maybe_unused]] motion_event const& event)
{
}

void event_spy::axis([[maybe_unused]] axis_event const& event)
{
}

void event_spy::key(key_event const& /*event*/)
{
}

void event_spy::key_repeat(key_event const& /*event*/)
{
}

void event_spy::touch_down(touch_down_event const& /*event*/)
{
}

void event_spy::touch_motion(touch_motion_event const& /*event*/)
{
}

void event_spy::touch_up(touch_up_event const& /*event*/)
{
}

void event_spy::pinch_begin(pinch_begin_event const& /*event*/)
{
}

void event_spy::pinch_update(pinch_update_event const& /*event*/)
{
}

void event_spy::pinch_end(pinch_end_event const& /*event*/)
{
}

void event_spy::swipe_begin(swipe_begin_event const& /*event*/)
{
}

void event_spy::swipe_update(swipe_update_event const& /*event*/)
{
}

void event_spy::swipe_end(swipe_end_event const& /*event*/)
{
}

void event_spy::switch_toggle(switch_toggle_event const& /*event*/)
{
}

void event_spy::tabletToolEvent(QTabletEvent* event)
{
    Q_UNUSED(event)
}

void event_spy::tabletToolButtonEvent(const QSet<uint>& pressedButtons)
{
    Q_UNUSED(pressedButtons)
}

void event_spy::tabletPadButtonEvent(const QSet<uint>& pressedButtons)
{
    Q_UNUSED(pressedButtons)
}

void event_spy::tabletPadStripEvent(int number, int position, bool isFinger)
{
    Q_UNUSED(number)
    Q_UNUSED(position)
    Q_UNUSED(isFinger)
}

void event_spy::tabletPadRingEvent(int number, int position, bool isFinger)
{
    Q_UNUSED(number)
    Q_UNUSED(position)
    Q_UNUSED(isFinger)
}
}
