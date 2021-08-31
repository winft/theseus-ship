/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "forward.h"

#include "../event.h"
#include "../keyboard_redirect.h"
#include "../qt_event.h"
#include "../redirect.h"
#include "../touch_redirect.h"
#include "main.h"
#include "wayland_server.h"
#include "workspace.h"
#include <input/pointer_redirect.h>

#include <Wrapland/Server/seat.h>

namespace KWin::input
{

bool forward_filter::key(key_event const& event)
{
    if (!workspace()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    kwinApp()->input->redirect->keyboard()->update();
    seat->setTimestamp(event.base.time_msec);
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

bool forward_filter::axis(axis_event const& event)
{
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event.base.time_msec);

    using wrap_source = Wrapland::Server::PointerAxisSource;

    auto source = wrap_source::Unknown;
    switch (event.source) {
    case axis_source::wheel:
        source = wrap_source::Wheel;
        break;
    case axis_source::finger:
        source = wrap_source::Finger;
        break;
    case axis_source::continuous:
        source = wrap_source::Continuous;
        break;
    case axis_source::wheel_tilt:
        source = wrap_source::WheelTilt;
        break;
    case axis_source::unknown:
    default:
        source = wrap_source::Unknown;
        break;
    }

    auto orientation = (event.orientation == axis_orientation::horizontal)
        ? Qt::Orientation::Horizontal
        : Qt::Orientation::Vertical;

    seat->pointerAxisV5(orientation, event.delta, event.delta_discrete, source);
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
