/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event_spy.h"

#include "input/redirect.h"
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

void event_spy::touchDown(qint32 id, const QPointF& point, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(point)
    Q_UNUSED(time)
}

void event_spy::touchMotion(qint32 id, const QPointF& point, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(point)
    Q_UNUSED(time)
}

void event_spy::touchUp(qint32 id, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(time)
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

void event_spy::swipeGestureBegin(int fingerCount, quint32 time)
{
    Q_UNUSED(fingerCount)
    Q_UNUSED(time)
}

void event_spy::swipeGestureUpdate(const QSizeF& delta, quint32 time)
{
    Q_UNUSED(delta)
    Q_UNUSED(time)
}

void event_spy::swipeGestureEnd(quint32 time)
{
    Q_UNUSED(time)
}

void event_spy::swipeGestureCancelled(quint32 time)
{
    Q_UNUSED(time)
}

void event_spy::switchEvent(SwitchEvent* event)
{
    Q_UNUSED(event)
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
