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
#include "input/xkb/helpers.h"
#include "win/deco.h"
#include "win/input.h"

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

template<typename Redirect>
class decoration_event_filter : public event_filter<Redirect>
{
public:
    explicit decoration_event_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool button(button_event const& event) override
    {
        auto decoration = this->redirect.pointer->focus.deco.client;
        if (!decoration) {
            return false;
        }

        assert(this->redirect.pointer->focus.deco.window);

        return std::visit(overload{[&](auto&& win) {
                              auto const action_result
                                  = perform_mouse_modifier_action(this->redirect, event, win);
                              if (action_result.first) {
                                  return action_result.second;
                              }

                              auto const global_pos = this->redirect.globalPointer();
                              auto const local_pos = global_pos - win->geo.pos();

                              auto qt_type = event.state == button_state::pressed
                                  ? QEvent::MouseButtonPress
                                  : QEvent::MouseButtonRelease;
                              auto qt_event = QMouseEvent(
                                  qt_type,
                                  local_pos,
                                  global_pos,
                                  button_to_qt_mouse_button(event.key),
                                  this->redirect.pointer->buttons(),
                                  xkb::get_active_keyboard_modifiers(this->redirect.platform));
                              qt_event.setAccepted(false);

                              QCoreApplication::sendEvent(decoration->decoration(), &qt_event);
                              if (!qt_event.isAccepted() && event.state == button_state::pressed) {
                                  win::process_decoration_button_press(win, &qt_event, false);
                              }
                              if (event.state == button_state::released) {
                                  win::process_decoration_button_release(win, &qt_event);
                              }
                              return true;
                          }},
                          *this->redirect.pointer->focus.deco.window);
    }

    bool motion(motion_event const& /*event*/) override
    {
        auto decoration = this->redirect.pointer->focus.deco.client;
        if (!decoration) {
            return false;
        }

        assert(this->redirect.pointer->focus.deco.window);

        return std::visit(
            overload{[&](auto&& win) {
                auto const global_pos = this->redirect.globalPointer();
                auto const local_pos = global_pos - win->geo.pos();

                auto qt_event = QHoverEvent(QEvent::HoverMove, local_pos, local_pos);
                QCoreApplication::instance()->sendEvent(decoration->decoration(), &qt_event);
                win::process_decoration_move(win, local_pos.toPoint(), global_pos.toPoint());
                return true;
            }},
            *this->redirect.pointer->focus.deco.window);
    }

    bool axis(axis_event const& event) override
    {
        auto decoration = this->redirect.pointer->focus.deco.client;
        if (!decoration) {
            return false;
        }

        assert(this->redirect.pointer->focus.deco.window);

        return std::visit(
            overload{[&](auto&& window) {
                if (event.orientation == axis_orientation::vertical) {
                    // client window action only on vertical scrolling
                    auto const actionResult = perform_wheel_action(this->redirect, event, window);
                    if (actionResult.first) {
                        return actionResult.second;
                    }
                }

                auto qt_event = axis_to_qt_event(*this->redirect.pointer, event);
                auto adapted_qt_event = QWheelEvent(qt_event.pos() - window->geo.pos(),
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
                    win::perform_mouse_command(
                        *window,
                        this->redirect.platform.base.options->operationTitlebarMouseWheel(
                            event.delta * -1),
                        this->redirect.pointer->pos().toPoint());
                }
                return true;
            }},
            *this->redirect.pointer->focus.deco.window);
    }

    bool touch_down(touch_down_event const& event) override
    {
        auto seat = this->redirect.platform.base.server->seat();
        if (seat->touches().is_in_progress()) {
            return false;
        }
        if (this->redirect.touch->decorationPressId() != -1) {
            // already on a decoration, ignore further touch points, but filter out
            return true;
        }
        seat->setTimestamp(event.base.time_msec);
        auto decoration = this->redirect.touch->focus.deco.client;
        if (!decoration) {
            return false;
        }

        assert(this->redirect.touch->focus.deco.window);

        return std::visit(
            overload{[&](auto&& win) {
                this->redirect.touch->setDecorationPressId(event.id);
                m_lastGlobalTouchPos = event.pos;
                m_lastLocalTouchPos = event.pos - win->geo.pos();

                QHoverEvent hoverEvent(QEvent::HoverMove, m_lastLocalTouchPos, m_lastLocalTouchPos);
                QCoreApplication::sendEvent(decoration->decoration(), &hoverEvent);

                QMouseEvent e(QEvent::MouseButtonPress,
                              m_lastLocalTouchPos,
                              event.pos,
                              Qt::LeftButton,
                              Qt::LeftButton,
                              xkb::get_active_keyboard_modifiers(this->redirect.platform));
                e.setAccepted(false);
                QCoreApplication::sendEvent(decoration->decoration(), &e);
                if (!e.isAccepted()) {
                    win::process_decoration_button_press(win, &e, false);
                }
                return true;
            }},
            *this->redirect.touch->focus.deco.window);
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        Q_UNUSED(time)
        auto decoration = this->redirect.touch->focus.deco.client;
        if (!decoration) {
            return false;
        }

        assert(this->redirect.touch->focus.deco.window);

        if (this->redirect.touch->decorationPressId() == -1) {
            return false;
        }
        if (this->redirect.touch->decorationPressId() != qint32(event.id)) {
            // ignore, but filter out
            return true;
        }

        return std::visit(overload{[&](auto&& win) {
                              m_lastGlobalTouchPos = event.pos;
                              m_lastLocalTouchPos = event.pos - win->geo.pos();

                              QHoverEvent e(
                                  QEvent::HoverMove, m_lastLocalTouchPos, m_lastLocalTouchPos);
                              QCoreApplication::instance()->sendEvent(decoration->decoration(), &e);
                              win::process_decoration_move(
                                  win, m_lastLocalTouchPos.toPoint(), event.pos.toPoint());
                              return true;
                          }},
                          *this->redirect.touch->focus.deco.window);
    }

    bool touch_up(touch_up_event const& event) override
    {
        Q_UNUSED(time);
        auto decoration = this->redirect.touch->focus.deco.client;
        if (!decoration) {
            return false;
        }

        assert(this->redirect.touch->focus.deco.window);

        if (this->redirect.touch->decorationPressId() == -1) {
            return false;
        }
        if (this->redirect.touch->decorationPressId() != qint32(event.id)) {
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
        QCoreApplication::sendEvent(decoration->decoration(), &e);

        std::visit(overload{[&](auto&& win) { win::process_decoration_button_release(win, &e); }},
                   *this->redirect.touch->focus.deco.window);

        QHoverEvent leaveEvent(QEvent::HoverLeave, QPointF(), QPointF());
        QCoreApplication::sendEvent(decoration->decoration(), &leaveEvent);

        m_lastGlobalTouchPos = QPointF();
        m_lastLocalTouchPos = QPointF();
        this->redirect.touch->setDecorationPressId(-1);
        return true;
    }

private:
    QPointF m_lastGlobalTouchPos;
    QPointF m_lastLocalTouchPos;
};

}
