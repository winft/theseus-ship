/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "internal_window.h"

#include "../keyboard_redirect.h"
#include "../pointer_redirect.h"
#include "../touch_redirect.h"
#include "helpers.h"
#include "main.h"
#include "screens.h"
#include "wayland_server.h"
#include "win/deco.h"
#include "win/internal_client.h"
#include "workspace.h"
#include <input/qt_event.h>

#include <QKeyEvent>
#include <QWindow>

namespace KWin::input
{

bool internal_window_filter::button(button_event const& event)
{
    auto internal = kwinApp()->input->redirect->pointer()->internalWindow();
    if (!internal) {
        return false;
    }

    auto window = qobject_cast<win::InternalClient*>(workspace()->findInternal(internal));

    if (window && win::decoration(window)) {
        // only perform mouse commands on decorated internal windows
        auto const actionResult = perform_mouse_modifier_action(event, window);
        if (actionResult.first) {
            return actionResult.second;
        }
    }

    auto qt_event = button_to_qt_event(event);
    auto adapted_qt_event = QMouseEvent(qt_event.type(),
                                        qt_event.pos() - internal->position(),
                                        qt_event.pos(),
                                        qt_event.button(),
                                        qt_event.buttons(),
                                        qt_event.modifiers());
    adapted_qt_event.setAccepted(false);
    QCoreApplication::sendEvent(internal, &adapted_qt_event);
    return adapted_qt_event.isAccepted();
}

bool internal_window_filter::motion(motion_event const& event)
{
    auto internal = kwinApp()->input->redirect->pointer()->internalWindow();
    if (!internal) {
        return false;
    }

    auto qt_event = motion_to_qt_event(event);
    auto adapted_qt_event = QMouseEvent(qt_event.type(),
                                        qt_event.pos() - internal->position(),
                                        qt_event.pos(),
                                        qt_event.button(),
                                        qt_event.buttons(),
                                        qt_event.modifiers());
    adapted_qt_event.setAccepted(false);
    QCoreApplication::sendEvent(internal, &adapted_qt_event);
    return adapted_qt_event.isAccepted();
}

bool internal_window_filter::wheelEvent(QWheelEvent* event)
{
    auto internal = kwinApp()->input->redirect->pointer()->internalWindow();
    if (!internal) {
        return false;
    }
    if (event->angleDelta().y() != 0) {
        auto s = qobject_cast<win::InternalClient*>(workspace()->findInternal(internal));
        if (s && win::decoration(s)) {
            // client window action only on vertical scrolling
            const auto actionResult = perform_client_wheel_action(event, s);
            if (actionResult.first) {
                return actionResult.second;
            }
        }
    }
    const QPointF localPos = event->globalPosF() - QPointF(internal->x(), internal->y());
    const Qt::Orientation orientation
        = (event->angleDelta().x() != 0) ? Qt::Horizontal : Qt::Vertical;
    const int delta
        = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y();
    QWheelEvent e(localPos,
                  event->globalPosF(),
                  QPoint(),
                  event->angleDelta() * -1,
                  delta * -1,
                  orientation,
                  event->buttons(),
                  event->modifiers());
    e.setAccepted(false);
    QCoreApplication::sendEvent(internal, &e);
    return e.isAccepted();
}

bool internal_window_filter::keyEvent(QKeyEvent* event)
{
    auto const& windows = workspace()->windows();
    if (windows.empty()) {
        return false;
    }
    QWindow* found = nullptr;
    auto it = windows.end();
    do {
        it--;
        auto internal = qobject_cast<win::InternalClient*>(*it);
        if (!internal) {
            continue;
        }
        auto w = internal->internalWindow();
        if (!w) {
            continue;
        }
        if (!w->isVisible()) {
            continue;
        }
        if (!screens()->geometry().contains(w->geometry())) {
            continue;
        }
        if (w->property("_q_showWithoutActivating").toBool()) {
            continue;
        }
        if (w->property("outputOnly").toBool()) {
            continue;
        }
        if (w->flags().testFlag(Qt::ToolTip)) {
            continue;
        }
        found = w;
        break;
    } while (it != windows.begin());
    if (!found) {
        return false;
    }
    auto xkb = kwinApp()->input->redirect->keyboard()->xkb();
    Qt::Key key = xkb->toQtKey(xkb->toKeysym(event->nativeScanCode()),
                               event->nativeScanCode(),
                               Qt::KeyboardModifiers(),
                               true /* workaround for QTBUG-62102 */);
    QKeyEvent internalEvent(event->type(),
                            key,
                            event->modifiers(),
                            event->nativeScanCode(),
                            event->nativeVirtualKey(),
                            event->nativeModifiers(),
                            event->text());
    internalEvent.setAccepted(false);
    if (QCoreApplication::sendEvent(found, &internalEvent)) {
        waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
        passToWaylandServer(event);
        return true;
    }
    return false;
}

bool internal_window_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (seat->isTouchSequence()) {
        // something else is getting the events
        return false;
    }
    auto touch = kwinApp()->input->redirect->touch();
    if (touch->internalPressId() != -1) {
        // already on internal window, ignore further touch points, but filter out
        m_pressedIds.insert(id);
        return true;
    }
    // a new touch point
    seat->setTimestamp(time);
    auto internal = touch->internalWindow();
    if (!internal) {
        return false;
    }
    touch->setInternalPressId(id);
    // Qt's touch event API is rather complex, let's do fake mouse events instead
    m_lastGlobalTouchPos = pos;
    m_lastLocalTouchPos = pos - QPointF(internal->x(), internal->y());

    QEnterEvent enterEvent(m_lastLocalTouchPos, m_lastLocalTouchPos, pos);
    QCoreApplication::sendEvent(internal, &enterEvent);

    QMouseEvent e(QEvent::MouseButtonPress,
                  m_lastLocalTouchPos,
                  pos,
                  Qt::LeftButton,
                  Qt::LeftButton,
                  kwinApp()->input->redirect->keyboardModifiers());
    e.setAccepted(false);
    QCoreApplication::sendEvent(internal, &e);
    return true;
}

bool internal_window_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    auto touch = kwinApp()->input->redirect->touch();
    auto internal = touch->internalWindow();
    if (!internal) {
        return false;
    }
    if (touch->internalPressId() == -1) {
        return false;
    }
    waylandServer()->seat()->setTimestamp(time);
    if (touch->internalPressId() != qint32(id) || m_pressedIds.contains(id)) {
        // ignore, but filter out
        return true;
    }
    m_lastGlobalTouchPos = pos;
    m_lastLocalTouchPos = pos - QPointF(internal->x(), internal->y());

    QMouseEvent e(QEvent::MouseMove,
                  m_lastLocalTouchPos,
                  m_lastGlobalTouchPos,
                  Qt::LeftButton,
                  Qt::LeftButton,
                  kwinApp()->input->redirect->keyboardModifiers());
    QCoreApplication::instance()->sendEvent(internal, &e);
    return true;
}

bool internal_window_filter::touchUp(qint32 id, quint32 time)
{
    auto touch = kwinApp()->input->redirect->touch();
    auto internal = touch->internalWindow();
    const bool removed = m_pressedIds.remove(id);
    if (!internal) {
        return removed;
    }
    if (touch->internalPressId() == -1) {
        return removed;
    }
    waylandServer()->seat()->setTimestamp(time);
    if (touch->internalPressId() != qint32(id)) {
        // ignore, but filter out
        return true;
    }
    // send mouse up
    QMouseEvent e(QEvent::MouseButtonRelease,
                  m_lastLocalTouchPos,
                  m_lastGlobalTouchPos,
                  Qt::LeftButton,
                  Qt::MouseButtons(),
                  kwinApp()->input->redirect->keyboardModifiers());
    e.setAccepted(false);
    QCoreApplication::sendEvent(internal, &e);

    QEvent leaveEvent(QEvent::Leave);
    QCoreApplication::sendEvent(internal, &leaveEvent);

    m_lastGlobalTouchPos = QPointF();
    m_lastLocalTouchPos = QPointF();
    kwinApp()->input->redirect->touch()->setInternalPressId(-1);
    return true;
}

}
