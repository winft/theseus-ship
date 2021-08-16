/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "forward.h"

#include "../event.h"
#include "../keyboard_redirect.h"
#include "../redirect.h"
#include "../touch_redirect.h"
#include "main.h"
#include "wayland_server.h"
#include "workspace.h"
#include <input/pointer_redirect.h>

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
    kwinApp()->input->redirect->keyboard()->update();
    seat->setTimestamp(event->timestamp());
    passToWaylandServer(event);
    return true;
}

bool forward_filter::button(button_event const& event)
{
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event.base.time_msec);

    switch (event.state) {
    case button_state::pressed:
        seat->pointerButtonPressed(event.key);
        break;
    case button_state::released:
        seat->pointerButtonReleased(event.key);
        break;
    }

    return true;
}

bool forward_filter::motion(motion_event const& event)
{
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event.base.time_msec);

    seat->setPointerPos(kwinApp()->input->redirect->pointer()->pos());
    if (!event.delta.isNull()) {
        seat->relativePointerMotion(QSizeF(event.delta.x(), event.delta.y()),
                                    QSizeF(event.unaccel_delta.x(), event.unaccel_delta.y()),
                                    event.base.time_msec);
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
    kwinApp()->input->redirect->touch()->insertId(id, seat->touchDown(pos));
    return true;
}
bool forward_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    const qint32 wraplandId = kwinApp()->input->redirect->touch()->mappedId(id);
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
    const qint32 wraplandId = kwinApp()->input->redirect->touch()->mappedId(id);
    if (wraplandId != -1) {
        seat->touchUp(wraplandId);
        kwinApp()->input->redirect->touch()->removeId(id);
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
    case redirect::PointerAxisSourceWheel:
        source = Wrapland::Server::PointerAxisSource::Wheel;
        break;
    case redirect::PointerAxisSourceFinger:
        source = Wrapland::Server::PointerAxisSource::Finger;
        break;
    case redirect::PointerAxisSourceContinuous:
        source = Wrapland::Server::PointerAxisSource::Continuous;
        break;
    case redirect::PointerAxisSourceWheelTilt:
        source = Wrapland::Server::PointerAxisSource::WheelTilt;
        break;
    case redirect::PointerAxisSourceUnknown:
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
