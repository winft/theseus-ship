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
    if (kwinApp()->input_redirect) {
        kwinApp()->input_redirect->uninstallInputEventSpy(this);
    }
}

void event_spy::pointerEvent(MouseEvent* event)
{
    Q_UNUSED(event)
}

void event_spy::wheelEvent(WheelEvent* event)
{
    Q_UNUSED(event)
}

void event_spy::keyEvent(KeyEvent* event)
{
    Q_UNUSED(event)
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

void event_spy::pinchGestureBegin(int fingerCount, quint32 time)
{
    Q_UNUSED(fingerCount)
    Q_UNUSED(time)
}

void event_spy::pinchGestureUpdate(qreal scale, qreal angleDelta, const QSizeF& delta, quint32 time)
{
    Q_UNUSED(scale)
    Q_UNUSED(angleDelta)
    Q_UNUSED(delta)
    Q_UNUSED(time)
}

void event_spy::pinchGestureEnd(quint32 time)
{
    Q_UNUSED(time)
}

void event_spy::pinchGestureCancelled(quint32 time)
{
    Q_UNUSED(time)
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
