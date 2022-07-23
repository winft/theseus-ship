/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/touch_redirect.h"
#include "input/xkb/helpers.h"
#include "main.h"
#include "win/deco.h"
#include "win/input.h"
#include "win/space.h"

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

class decoration_event_filter : public event_filter
{
public:
    bool button(button_event const& event) override
    {
        auto decoration = kwinApp()->input->redirect->pointer()->focus.deco;
        if (!decoration) {
            return false;
        }

        auto const action_result = perform_mouse_modifier_action(event, decoration->client());
        if (action_result.first) {
            return action_result.second;
        }

        auto const global_pos = kwinApp()->input->redirect->globalPointer();
        auto const local_pos = global_pos - decoration->client()->pos();

        auto qt_type = event.state == button_state::pressed ? QEvent::MouseButtonPress
                                                            : QEvent::MouseButtonRelease;
        auto qt_event = QMouseEvent(qt_type,
                                    local_pos,
                                    global_pos,
                                    button_to_qt_mouse_button(event.key),
                                    kwinApp()->input->redirect->pointer()->buttons(),
                                    xkb::get_active_keyboard_modifiers(kwinApp()->input));
        qt_event.setAccepted(false);

        QCoreApplication::sendEvent(decoration->decoration(), &qt_event);
        if (!qt_event.isAccepted() && event.state == button_state::pressed) {
            win::process_decoration_button_press(decoration->client(), &qt_event, false);
        }
        if (event.state == button_state::released) {
            win::process_decoration_button_release(decoration->client(), &qt_event);
        }
        return true;
    }

    bool motion(motion_event const& /*event*/) override
    {
        auto decoration = kwinApp()->input->redirect->pointer()->focus.deco;
        if (!decoration) {
            return false;
        }

        auto const global_pos = kwinApp()->input->redirect->globalPointer();
        auto const local_pos = global_pos - decoration->client()->pos();

        auto qt_event = QHoverEvent(QEvent::HoverMove, local_pos, local_pos);
        QCoreApplication::instance()->sendEvent(decoration->decoration(), &qt_event);
        win::process_decoration_move(
            decoration->client(), local_pos.toPoint(), global_pos.toPoint());
        return true;
    }

    bool axis(axis_event const& event) override
    {
        auto decoration = kwinApp()->input->redirect->pointer()->focus.deco;
        if (!decoration) {
            return false;
        }

        auto window = decoration->client();

        if (event.orientation == axis_orientation::vertical) {
            // client window action only on vertical scrolling
            auto const actionResult = perform_wheel_action(event, window);
            if (actionResult.first) {
                return actionResult.second;
            }
        }

        auto qt_event = axis_to_qt_event(event);
        auto adapted_qt_event = QWheelEvent(qt_event.pos() - window->pos(),
                                            qt_event.pos(),
                                            QPoint(),
                                            qt_event.angleDelta(),
                                            qt_event.delta(),
                                            qt_event.orientation(),
                                            qt_event.buttons(),
                                            qt_event.modifiers());

        adapted_qt_event.setAccepted(false);
        QCoreApplication::sendEvent(decoration->decoration(), &adapted_qt_event);

        if (adapted_qt_event.isAccepted()) {
            return true;
        }

        if ((event.orientation == axis_orientation::vertical)
            && win::titlebar_positioned_under_mouse(window)) {
            window->performMouseCommand(
                kwinApp()->options->operationTitlebarMouseWheel(event.delta * -1),
                kwinApp()->input->redirect->pointer()->pos().toPoint());
        }
        return true;
    }

    bool touch_down(touch_down_event const& event) override
    {
        auto seat = waylandServer()->seat();
        if (seat->touches().is_in_progress()) {
            return false;
        }
        if (kwinApp()->input->redirect->touch()->decorationPressId() != -1) {
            // already on a decoration, ignore further touch points, but filter out
            return true;
        }
        seat->setTimestamp(event.base.time_msec);
        auto decoration = kwinApp()->input->redirect->touch()->focus.deco;
        if (!decoration) {
            return false;
        }

        kwinApp()->input->redirect->touch()->setDecorationPressId(event.id);
        m_lastGlobalTouchPos = event.pos;
        m_lastLocalTouchPos = event.pos - decoration->client()->pos();

        QHoverEvent hoverEvent(QEvent::HoverMove, m_lastLocalTouchPos, m_lastLocalTouchPos);
        QCoreApplication::sendEvent(decoration->decoration(), &hoverEvent);

        QMouseEvent e(QEvent::MouseButtonPress,
                      m_lastLocalTouchPos,
                      event.pos,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      xkb::get_active_keyboard_modifiers(kwinApp()->input));
        e.setAccepted(false);
        QCoreApplication::sendEvent(decoration->decoration(), &e);
        if (!e.isAccepted()) {
            win::process_decoration_button_press(decoration->client(), &e, false);
        }
        return true;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        Q_UNUSED(time)
        auto decoration = kwinApp()->input->redirect->touch()->focus.deco;
        if (!decoration) {
            return false;
        }
        if (kwinApp()->input->redirect->touch()->decorationPressId() == -1) {
            return false;
        }
        if (kwinApp()->input->redirect->touch()->decorationPressId() != qint32(event.id)) {
            // ignore, but filter out
            return true;
        }
        m_lastGlobalTouchPos = event.pos;
        m_lastLocalTouchPos = event.pos - decoration->client()->pos();

        QHoverEvent e(QEvent::HoverMove, m_lastLocalTouchPos, m_lastLocalTouchPos);
        QCoreApplication::instance()->sendEvent(decoration->decoration(), &e);
        win::process_decoration_move(
            decoration->client(), m_lastLocalTouchPos.toPoint(), event.pos.toPoint());
        return true;
    }

    bool touch_up(touch_up_event const& event) override
    {
        Q_UNUSED(time);
        auto decoration = kwinApp()->input->redirect->touch()->focus.deco;
        if (!decoration) {
            return false;
        }
        if (kwinApp()->input->redirect->touch()->decorationPressId() == -1) {
            return false;
        }
        if (kwinApp()->input->redirect->touch()->decorationPressId() != qint32(event.id)) {
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
        QCoreApplication::sendEvent(decoration->decoration(), &e);
        win::process_decoration_button_release(decoration->client(), &e);

        QHoverEvent leaveEvent(QEvent::HoverLeave, QPointF(), QPointF());
        QCoreApplication::sendEvent(decoration->decoration(), &leaveEvent);

        m_lastGlobalTouchPos = QPointF();
        m_lastLocalTouchPos = QPointF();
        kwinApp()->input->redirect->touch()->setDecorationPressId(-1);
        return true;
    }

private:
    QPointF m_lastGlobalTouchPos;
    QPointF m_lastLocalTouchPos;
};

}
