/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lock_screen.h"

#include "../keyboard_redirect.h"
#include "../touch_redirect.h"
#include "input/qt_event.h"
#include "main.h"
#include "toplevel.h"
#include "wayland_server.h"

#include <KScreenLocker/KsldApp>

#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

bool lock_screen_filter::button(button_event const& event)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }

    auto seat = waylandServer()->seat();
    seat->setTimestamp(event.base.time_msec);

    if (pointerSurfaceAllowed()) {
        // TODO: can we leak presses/releases here when we move the mouse in between from an
        // allowed surface to disallowed one or vice versa?
        event.state == button_state::pressed ? seat->pointers().button_pressed(event.key)
                                             : seat->pointers().button_released(event.key);
    }

    return true;
}

bool lock_screen_filter::motion(motion_event const& event)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }

    auto seat = waylandServer()->seat();
    seat->setTimestamp(event.base.time_msec);

    if (pointerSurfaceAllowed()) {
        // TODO: should the pointer position always stay in sync, i.e. not do the check?
        auto pos = kwinApp()->input->redirect->globalPointer();
        seat->pointers().set_position(pos.toPoint());
    }

    return true;
}

bool lock_screen_filter::axis(axis_event const& event)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }

    auto seat = waylandServer()->seat();
    if (pointerSurfaceAllowed()) {
        seat->setTimestamp(event.base.time_msec);

        auto orientation
            = (event.orientation == axis_orientation::horizontal) ? Qt::Horizontal : Qt::Vertical;
        seat->pointers().send_axis(orientation, event.delta);
    }
    return true;
}

bool lock_screen_filter::key(key_event const& event)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }

    // send event to KSldApp for global accel
    // if event is set to accepted it means a whitelisted shortcut was triggered
    // in that case we filter it out and don't process it further
    auto qt_event = key_to_qt_event(event);
    qt_event.setAccepted(false);
    QCoreApplication::sendEvent(ScreenLocker::KSldApp::self(), &qt_event);
    if (qt_event.isAccepted()) {
        return true;
    }

    // continue normal processing
    kwinApp()->input->redirect->keyboard()->update();

    auto seat = waylandServer()->seat();
    seat->setTimestamp(event.base.time_msec);

    if (!keyboardSurfaceAllowed()) {
        // don't pass event to seat
        return true;
    }

    switch (event.state) {
    case button_state::pressed:
        seat->keyboards().key_pressed(event.keycode);
        break;
    case button_state::released:
        seat->keyboards().key_released(event.keycode);
        break;
    }
    return true;
}

bool lock_screen_filter::key_repeat(key_event const& /*event*/)
{
    // If screen is locked Wayland client takes care of it.
    return waylandServer()->isScreenLocked();
}

bool lock_screen_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    if (touchSurfaceAllowed()) {
        kwinApp()->input->redirect->touch()->insertId(id, seat->touches().touch_down(pos));
    }
    return true;
}
bool lock_screen_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    if (touchSurfaceAllowed()) {
        const qint32 wraplandId = kwinApp()->input->redirect->touch()->mappedId(id);
        if (wraplandId != -1) {
            seat->touches().touch_move(wraplandId, pos);
        }
    }
    return true;
}
bool lock_screen_filter::touchUp(qint32 id, quint32 time)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    if (touchSurfaceAllowed()) {
        const qint32 wraplandId = kwinApp()->input->redirect->touch()->mappedId(id);
        if (wraplandId != -1) {
            seat->touches().touch_up(wraplandId);
            kwinApp()->input->redirect->touch()->removeId(id);
        }
    }
    return true;
}
bool lock_screen_filter::pinch_begin(pinch_begin_event const& /*event*/)
{
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::pinch_update(pinch_update_event const& /*event*/)
{
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::pinch_end(pinch_end_event const& /*event*/)
{
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}

bool lock_screen_filter::swipe_begin(swipe_begin_event const& /*event*/)
{
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::swipe_update(swipe_update_event const& /*event*/)
{
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::swipeGestureEnd(quint32 time)
{
    Q_UNUSED(time)
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::swipeGestureCancelled(quint32 time)
{
    Q_UNUSED(time)
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}

template<typename Pool>
bool is_surface_allowed(Pool const& device_pool)
{
    if (auto surface = device_pool.get_focus().surface) {
        if (auto win = waylandServer()->findToplevel(surface)) {
            return win->isLockScreen() || win->isInputMethod();
        }
        return false;
    }
    return true;
}

bool lock_screen_filter::pointerSurfaceAllowed() const
{
    return is_surface_allowed(waylandServer()->seat()->pointers());
}

bool lock_screen_filter::keyboardSurfaceAllowed() const
{
    return is_surface_allowed(waylandServer()->seat()->keyboards());
}

bool lock_screen_filter::touchSurfaceAllowed() const
{
    return is_surface_allowed(waylandServer()->seat()->touches());
}

}
