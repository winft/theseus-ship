/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/platform.h"
#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/touch_redirect.h"
#include "input/xkb/helpers.h"
#include "win/deco.h"
#include "win/internal_window.h"
#include "win/space.h"

#include <QWindow>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

template<typename Redirect>
class internal_window_filter : public event_filter<Redirect>
{
public:
    explicit internal_window_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool button(button_event const& event) override
    {
        auto internal = this->redirect.pointer->focus.internal_window;
        if (!internal) {
            return false;
        }

        auto window
            = qobject_cast<win::internal_window*>(this->redirect.space.findInternal(internal));

        if (window && win::decoration(window)) {
            // only perform mouse commands on decorated internal windows
            auto const actionResult = perform_mouse_modifier_action(this->redirect, event, window);
            if (actionResult.first) {
                return actionResult.second;
            }
        }

        auto qt_event = button_to_qt_event(*this->redirect.pointer, event);
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

    bool motion(motion_event const& event) override
    {
        auto internal = this->redirect.pointer->focus.internal_window;
        if (!internal) {
            return false;
        }

        auto qt_event = motion_to_qt_event(*this->redirect.pointer, event);
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

    bool axis(axis_event const& event) override
    {
        auto internal = this->redirect.pointer->focus.internal_window;
        if (!internal) {
            return false;
        }

        if (event.orientation == axis_orientation::vertical) {
            auto window
                = qobject_cast<win::internal_window*>(this->redirect.space.findInternal(internal));
            if (window && win::decoration(window)) {
                // client window action only on vertical scrolling
                auto const action_result
                    = perform_wheel_and_window_action(this->redirect, event, window);
                if (action_result.first) {
                    return action_result.second;
                }
            }
        }

        auto qt_event = axis_to_qt_event(*this->redirect.pointer, event);
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

    QWindow* get_internal_window(std::vector<Toplevel*> const& windows)
    {
        if (windows.empty()) {
            return nullptr;
        }

        auto const& base = kwinApp()->get_base();
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
            if (!QRect({}, base.topology.size).contains(w->geometry())) {
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

    bool key(key_event const& event) override
    {
        auto window = get_internal_window(this->redirect.space.windows);
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

    bool key_repeat(key_event const& event) override
    {
        auto window = get_internal_window(this->redirect.space.windows);
        if (!window) {
            return false;
        }

        auto internal_event = get_internal_key_event(event);
        return QCoreApplication::sendEvent(window, &internal_event);
    }

    bool touch_down(touch_down_event const& event) override
    {
        auto seat = waylandServer()->seat();
        if (seat->touches().is_in_progress()) {
            // something else is getting the events
            return false;
        }
        auto& touch = this->redirect.touch;
        if (touch->internalPressId() != -1) {
            // already on internal window, ignore further touch points, but filter out
            m_pressedIds.insert(event.id);
            return true;
        }
        // a new touch point
        seat->setTimestamp(event.base.time_msec);
        auto internal = touch->focus.internal_window;
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
                      xkb::get_active_keyboard_modifiers(this->redirect.platform));
        e.setAccepted(false);
        QCoreApplication::sendEvent(internal, &e);
        return true;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        auto& touch = this->redirect.touch;
        auto internal = touch->focus.internal_window;
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
                      xkb::get_active_keyboard_modifiers(this->redirect.platform));
        QCoreApplication::instance()->sendEvent(internal, &e);
        return true;
    }

    bool touch_up(touch_up_event const& event) override
    {
        auto& touch = this->redirect.touch;
        auto internal = touch->focus.internal_window;
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
                      xkb::get_active_keyboard_modifiers(this->redirect.platform));
        e.setAccepted(false);
        QCoreApplication::sendEvent(internal, &e);

        QEvent leaveEvent(QEvent::Leave);
        QCoreApplication::sendEvent(internal, &leaveEvent);

        m_lastGlobalTouchPos = QPointF();
        m_lastLocalTouchPos = QPointF();
        this->redirect.touch->setInternalPressId(-1);
        return true;
    }

private:
    QSet<qint32> m_pressedIds;
    QPointF m_lastGlobalTouchPos;
    QPointF m_lastLocalTouchPos;
};

}
