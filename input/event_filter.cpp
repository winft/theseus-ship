/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event_filter.h"

#include "input/redirect.h"
#include "main.h"
#include "wayland_server.h"

#include <Wrapland/Server/seat.h>

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

bool event_filter::wheelEvent(QWheelEvent* event)
{
    Q_UNUSED(event)
    return false;
}

bool event_filter::keyEvent(QKeyEvent* event)
{
    Q_UNUSED(event)
    return false;
}

bool event_filter::touchDown(qint32 id, const QPointF& point, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(point)
    Q_UNUSED(time)
    return false;
}

bool event_filter::touchMotion(qint32 id, const QPointF& point, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(point)
    Q_UNUSED(time)
    return false;
}

bool event_filter::touchUp(qint32 id, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(time)
    return false;
}

bool event_filter::pinchGestureBegin(int fingerCount, quint32 time)
{
    Q_UNUSED(fingerCount)
    Q_UNUSED(time)
    return false;
}

bool event_filter::pinchGestureUpdate(qreal scale,
                                      qreal angleDelta,
                                      const QSizeF& delta,
                                      quint32 time)
{
    Q_UNUSED(scale)
    Q_UNUSED(angleDelta)
    Q_UNUSED(delta)
    Q_UNUSED(time)
    return false;
}

bool event_filter::pinchGestureEnd(quint32 time)
{
    Q_UNUSED(time)
    return false;
}

bool event_filter::pinchGestureCancelled(quint32 time)
{
    Q_UNUSED(time)
    return false;
}

bool event_filter::swipeGestureBegin(int fingerCount, quint32 time)
{
    Q_UNUSED(fingerCount)
    Q_UNUSED(time)
    return false;
}

bool event_filter::swipeGestureUpdate(const QSizeF& delta, quint32 time)
{
    Q_UNUSED(delta)
    Q_UNUSED(time)
    return false;
}

bool event_filter::swipeGestureEnd(quint32 time)
{
    Q_UNUSED(time)
    return false;
}

bool event_filter::swipeGestureCancelled(quint32 time)
{
    Q_UNUSED(time)
    return false;
}

bool event_filter::switchEvent(SwitchEvent* event)
{
    Q_UNUSED(event)
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

void event_filter::passToWaylandServer(QKeyEvent* event)
{
    Q_ASSERT(waylandServer());
    if (event->isAutoRepeat()) {
        return;
    }
    switch (event->type()) {
    case QEvent::KeyPress:
        waylandServer()->seat()->keyPressed(event->nativeScanCode());
        break;
    case QEvent::KeyRelease:
        waylandServer()->seat()->keyReleased(event->nativeScanCode());
        break;
    default:
        break;
    }
}

}
