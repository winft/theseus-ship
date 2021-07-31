/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lock_screen.h"

#include "main.h"
#include "toplevel.h"
#include "touch_input.h"
#include "wayland_server.h"
#include "xkb.h"

#include <KScreenLocker/KsldApp>

#include <Wrapland/Server/seat.h>

#include <QKeyEvent>

namespace KWin::input
{

bool lock_screen_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event->timestamp());
    if (event->type() == QEvent::MouseMove) {
        if (pointerSurfaceAllowed()) {
            // TODO: should the pointer position always stay in sync, i.e. not do the check?
            seat->setPointerPos(event->screenPos().toPoint());
        }
    } else if (event->type() == QEvent::MouseButtonPress
               || event->type() == QEvent::MouseButtonRelease) {
        if (pointerSurfaceAllowed()) {
            // TODO: can we leak presses/releases here when we move the mouse in between from an
            // allowed surface to
            //       disallowed one or vice versa?
            event->type() == QEvent::MouseButtonPress ? seat->pointerButtonPressed(nativeButton)
                                                      : seat->pointerButtonReleased(nativeButton);
        }
    }
    return true;
}
bool lock_screen_filter::wheelEvent(QWheelEvent* event)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    if (pointerSurfaceAllowed()) {
        seat->setTimestamp(event->timestamp());
        const Qt::Orientation orientation
            = event->angleDelta().x() == 0 ? Qt::Vertical : Qt::Horizontal;
        seat->pointerAxis(orientation,
                          orientation == Qt::Horizontal ? event->angleDelta().x()
                                                        : event->angleDelta().y());
    }
    return true;
}
bool lock_screen_filter::keyEvent(QKeyEvent* event)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    if (event->isAutoRepeat()) {
        // wayland client takes care of it
        return true;
    }
    // send event to KSldApp for global accel
    // if event is set to accepted it means a whitelisted shortcut was triggered
    // in that case we filter it out and don't process it further
    event->setAccepted(false);
    QCoreApplication::sendEvent(ScreenLocker::KSldApp::self(), event);
    if (event->isAccepted()) {
        return true;
    }

    // continue normal processing
    kwinApp()->input_redirect->keyboard()->update();
    auto seat = waylandServer()->seat();
    seat->setTimestamp(event->timestamp());
    if (!keyboardSurfaceAllowed()) {
        // don't pass event to seat
        return true;
    }
    switch (event->type()) {
    case QEvent::KeyPress:
        seat->keyPressed(event->nativeScanCode());
        break;
    case QEvent::KeyRelease:
        seat->keyReleased(event->nativeScanCode());
        break;
    default:
        break;
    }
    return true;
}
bool lock_screen_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    if (!waylandServer()->isScreenLocked()) {
        return false;
    }
    auto seat = waylandServer()->seat();
    seat->setTimestamp(time);
    if (touchSurfaceAllowed()) {
        kwinApp()->input_redirect->touch()->insertId(id, seat->touchDown(pos));
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
        const qint32 wraplandId = kwinApp()->input_redirect->touch()->mappedId(id);
        if (wraplandId != -1) {
            seat->touchMove(wraplandId, pos);
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
        const qint32 wraplandId = kwinApp()->input_redirect->touch()->mappedId(id);
        if (wraplandId != -1) {
            seat->touchUp(wraplandId);
            kwinApp()->input_redirect->touch()->removeId(id);
        }
    }
    return true;
}
bool lock_screen_filter::pinchGestureBegin(int fingerCount, quint32 time)
{
    Q_UNUSED(fingerCount)
    Q_UNUSED(time)
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::pinchGestureUpdate(qreal scale,
                                            qreal angleDelta,
                                            const QSizeF& delta,
                                            quint32 time)
{
    Q_UNUSED(scale)
    Q_UNUSED(angleDelta)
    Q_UNUSED(delta)
    Q_UNUSED(time)
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::pinchGestureEnd(quint32 time)
{
    Q_UNUSED(time)
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::pinchGestureCancelled(quint32 time)
{
    Q_UNUSED(time)
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}

bool lock_screen_filter::swipeGestureBegin(int fingerCount, quint32 time)
{
    Q_UNUSED(fingerCount)
    Q_UNUSED(time)
    // no touchpad multi-finger gestures on lock screen
    return waylandServer()->isScreenLocked();
}
bool lock_screen_filter::swipeGestureUpdate(const QSizeF& delta, quint32 time)
{
    Q_UNUSED(delta)
    Q_UNUSED(time)
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

bool lock_screen_filter::surfaceAllowed(
    Wrapland::Server::Surface* (Wrapland::Server::Seat::*method)() const) const
{
    if (Wrapland::Server::Surface* s = (waylandServer()->seat()->*method)()) {
        if (auto win = waylandServer()->findToplevel(s)) {
            return win->isLockScreen() || win->isInputMethod();
        }
        return false;
    }
    return true;
}

bool lock_screen_filter::pointerSurfaceAllowed() const
{
    return surfaceAllowed(&Wrapland::Server::Seat::focusedPointerSurface);
}

bool lock_screen_filter::keyboardSurfaceAllowed() const
{
    return surfaceAllowed(&Wrapland::Server::Seat::focusedKeyboardSurface);
}

bool lock_screen_filter::touchSurfaceAllowed() const
{
    return surfaceAllowed(&Wrapland::Server::Seat::focusedTouchSurface);
}

}
