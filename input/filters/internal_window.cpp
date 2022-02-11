/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "internal_window.h"

#include "helpers.h"

#include "base/platform.h"
#include "base/wayland/server.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/touch_redirect.h"
#include "input/xkb/helpers.h"
#include "main.h"
#include "screens.h"
#include "win/deco.h"
#include "win/internal_window.h"
#include "win/space.h"

#include <QWindow>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

bool internal_window_filter::button(button_event const& event)
{
    auto internal = kwinApp()->input->redirect->pointer()->internalWindow();
    if (!internal) {
        return false;
    }

    auto window = qobject_cast<win::internal_window*>(workspace()->findInternal(internal));

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

bool internal_window_filter::axis(axis_event const& event)
{
    auto internal = kwinApp()->input->redirect->pointer()->internalWindow();
    if (!internal) {
        return false;
    }

    if (event.orientation == axis_orientation::vertical) {
        auto window = qobject_cast<win::internal_window*>(workspace()->findInternal(internal));
        if (window && win::decoration(window)) {
            // client window action only on vertical scrolling
            auto const action_result = perform_wheel_and_window_action(event, window);
            if (action_result.first) {
                return action_result.second;
            }
        }
    }

    auto qt_event = axis_to_qt_event(event);
    auto adapted_qt_event = QWheelEvent(qt_event.pos() - internal->position(),
                                        qt_event.pos(),
                                        QPoint(),
                                        qt_event.angleDelta() * -1,
                                        qt_event.delta() * -1,
                                        qt_event.orientation(),
                                        qt_event.buttons(),
                                        qt_event.modifiers());

    adapted_qt_event.setAccepted(false);
    QCoreApplication::sendEvent(internal, &adapted_qt_event);
    return adapted_qt_event.isAccepted();
}

QWindow* get_internal_window()
{
    auto const& windows = workspace()->windows();
    if (windows.empty()) {
        return nullptr;
    }

    QWindow* found = nullptr;
    auto it = windows.end();

    do {
        it--;
        auto internal = qobject_cast<win::internal_window*>(*it);
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
        if (!kwinApp()->get_base().screens.geometry().contains(w->geometry())) {
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

    return found;
}

QKeyEvent get_internal_key_event(key_event const& event)
{
    auto const& xkb = event.base.dev->xkb;
    auto const keysym = xkb->to_keysym(event.keycode);
    auto qt_key = xkb->to_qt_key(
        keysym, event.keycode, Qt::KeyboardModifiers(), true /* workaround for QTBUG-62102 */);

    QKeyEvent internalEvent(event.state == key_state::pressed ? QEvent::KeyPress
                                                              : QEvent::KeyRelease,
                            qt_key,
                            xkb->qt_modifiers,
                            event.keycode,
                            keysym,
                            0,
                            QString::fromStdString(xkb->to_string(keysym)));
    internalEvent.setAccepted(false);

    return internalEvent;
}

bool internal_window_filter::key(key_event const& event)
{
    auto window = get_internal_window();
    if (!window) {
        return false;
    }

    auto internal_event = get_internal_key_event(event);
    if (QCoreApplication::sendEvent(window, &internal_event)) {
        waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
        pass_to_wayland_server(event);
        return true;
    }
    return false;
}

bool internal_window_filter::key_repeat(key_event const& event)
{
    auto window = get_internal_window();
    if (!window) {
        return false;
    }

    auto internal_event = get_internal_key_event(event);
    return QCoreApplication::sendEvent(window, &internal_event);
}

bool internal_window_filter::touch_down(touch_down_event const& event)
{
    auto seat = waylandServer()->seat();
    if (seat->touches().is_in_progress()) {
        // something else is getting the events
        return false;
    }
    auto touch = kwinApp()->input->redirect->touch();
    if (touch->internalPressId() != -1) {
        // already on internal window, ignore further touch points, but filter out
        m_pressedIds.insert(event.id);
        return true;
    }
    // a new touch point
    seat->setTimestamp(event.base.time_msec);
    auto internal = touch->internalWindow();
    if (!internal) {
        return false;
    }
    touch->setInternalPressId(event.id);
    // Qt's touch event API is rather complex, let's do fake mouse events instead
    m_lastGlobalTouchPos = event.pos;
    m_lastLocalTouchPos = event.pos - QPointF(internal->x(), internal->y());

    QEnterEvent enterEvent(m_lastLocalTouchPos, m_lastLocalTouchPos, event.pos);
    QCoreApplication::sendEvent(internal, &enterEvent);

    QMouseEvent e(QEvent::MouseButtonPress,
                  m_lastLocalTouchPos,
                  event.pos,
                  Qt::LeftButton,
                  Qt::LeftButton,
                  xkb::get_active_keyboard_modifiers(kwinApp()->input));
    e.setAccepted(false);
    QCoreApplication::sendEvent(internal, &e);
    return true;
}

bool internal_window_filter::touch_motion(touch_motion_event const& event)
{
    auto touch = kwinApp()->input->redirect->touch();
    auto internal = touch->internalWindow();
    if (!internal) {
        return false;
    }
    if (touch->internalPressId() == -1) {
        return false;
    }
    waylandServer()->seat()->setTimestamp(event.base.time_msec);
    if (touch->internalPressId() != qint32(event.id) || m_pressedIds.contains(event.id)) {
        // ignore, but filter out
        return true;
    }
    m_lastGlobalTouchPos = event.pos;
    m_lastLocalTouchPos = event.pos - QPointF(internal->x(), internal->y());

    QMouseEvent e(QEvent::MouseMove,
                  m_lastLocalTouchPos,
                  m_lastGlobalTouchPos,
                  Qt::LeftButton,
                  Qt::LeftButton,
                  xkb::get_active_keyboard_modifiers(kwinApp()->input));
    QCoreApplication::instance()->sendEvent(internal, &e);
    return true;
}

bool internal_window_filter::touch_up(touch_up_event const& event)
{
    auto touch = kwinApp()->input->redirect->touch();
    auto internal = touch->internalWindow();
    const bool removed = m_pressedIds.remove(event.id);
    if (!internal) {
        return removed;
    }
    if (touch->internalPressId() == -1) {
        return removed;
    }
    waylandServer()->seat()->setTimestamp(event.base.time_msec);
    if (touch->internalPressId() != qint32(event.id)) {
        // ignore, but filter out
        return true;
    }
    // send mouse up
    QMouseEvent e(QEvent::MouseButtonRelease,
                  m_lastLocalTouchPos,
                  m_lastGlobalTouchPos,
                  Qt::LeftButton,
                  Qt::MouseButtons(),
                  xkb::get_active_keyboard_modifiers(kwinApp()->input));
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
