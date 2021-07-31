/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "forward.h"

#include "input_event.h"
#include "main.h"
#include "touch_input.h"
#include "wayland_server.h"
#include "workspace.h"

#include <Wrapland/Server/seat.h>

#include <QKeyEvent>

namespace KWin::input
{

bool forward_filter::keyEvent(QKeyEvent* event)
{
    if (!workspace()) {
        return false;
    }
    if (event->isAutoRepeat()) {
        // handled by Wayland client
        return false;
    }
    auto seat = waylandServer()->seat();
    kwinApp()->input_redirect->keyboard()->update();
    seat->setTimestamp(event->timestamp());
    passToWaylandServer(event);
    return true;
}

bool forward_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event->timestamp());
    switch (event->type()) {
    case QEvent::MouseMove: {
        seat->setPointerPos(event->globalPos());
        MouseEvent* e = static_cast<MouseEvent*>(event);
        if (e->delta() != QSizeF()) {
            seat->relativePointerMotion(
                e->delta(), e->deltaUnaccelerated(), e->timestampMicroseconds());
        }
        break;
    }
    case QEvent::MouseButtonPress:
        seat->pointerButtonPressed(nativeButton);
        break;
    case QEvent::MouseButtonRelease:
        seat->pointerButtonReleased(nativeButton);
        break;
    default:
        break;
    }
    return true;
}

bool forward_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    kwinApp()->input_redirect->touch()->insertId(id, seat->touchDown(pos));
    return true;
}
bool forward_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    const qint32 wraplandId = kwinApp()->input_redirect->touch()->mappedId(id);
    if (wraplandId != -1) {
        seat->touchMove(wraplandId, pos);
    }
    return true;
}
bool forward_filter::touchUp(qint32 id, quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    const qint32 wraplandId = kwinApp()->input_redirect->touch()->mappedId(id);
    if (wraplandId != -1) {
        seat->touchUp(wraplandId);
        kwinApp()->input_redirect->touch()->removeId(id);
    }
    return true;
}

bool forward_filter::wheelEvent(QWheelEvent* event)
{
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event->timestamp());
    auto _event = static_cast<WheelEvent*>(event);
    Wrapland::Server::PointerAxisSource source;
    switch (_event->axisSource()) {
    case KWin::InputRedirection::PointerAxisSourceWheel:
        source = Wrapland::Server::PointerAxisSource::Wheel;
        break;
    case KWin::InputRedirection::PointerAxisSourceFinger:
        source = Wrapland::Server::PointerAxisSource::Finger;
        break;
    case KWin::InputRedirection::PointerAxisSourceContinuous:
        source = Wrapland::Server::PointerAxisSource::Continuous;
        break;
    case KWin::InputRedirection::PointerAxisSourceWheelTilt:
        source = Wrapland::Server::PointerAxisSource::WheelTilt;
        break;
    case KWin::InputRedirection::PointerAxisSourceUnknown:
    default:
        source = Wrapland::Server::PointerAxisSource::Unknown;
        break;
    }
    seat->pointerAxisV5(_event->orientation(), _event->delta(), _event->discreteDelta(), source);
    return true;
}

bool forward_filter::pinchGestureBegin(int fingerCount, quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    seat->startPointerPinchGesture(fingerCount);
    return true;
}

bool forward_filter::pinchGestureUpdate(qreal scale,
                                        qreal angleDelta,
                                        const QSizeF& delta,
                                        quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    seat->updatePointerPinchGesture(delta, scale, angleDelta);
    return true;
}

bool forward_filter::pinchGestureEnd(quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    seat->endPointerPinchGesture();
    return true;
}

bool forward_filter::pinchGestureCancelled(quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    seat->cancelPointerPinchGesture();
    return true;
}

bool forward_filter::swipeGestureBegin(int fingerCount, quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    seat->startPointerSwipeGesture(fingerCount);
    return true;
}

bool forward_filter::swipeGestureUpdate(const QSizeF& delta, quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    seat->updatePointerSwipeGesture(delta);
    return true;
}

bool forward_filter::swipeGestureEnd(quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    seat->endPointerSwipeGesture();
    return true;
}

bool forward_filter::swipeGestureCancelled(quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    seat->cancelPointerSwipeGesture();
    return true;
}

}
