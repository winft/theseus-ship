/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/pointer_redirect.h"
#include "main.h"
#include "win/input.h"
#include "win/transient.h"

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

/**
 * This filter implements window actions. If the event should not be passed to the
 * current pointer window it will filter out the event
 */
template<typename Redirect>
class window_action_filter : public event_filter<Redirect>
{
public:
    explicit window_action_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool button(button_event const& event) override
    {
        if (event.state != button_state::pressed) {
            return false;
        }

        if (!this->redirect.pointer->focus.window) {
            return false;
        }

        return std::visit(overload{[&](auto&& win) {
                              auto focus_window = get_focus_lead(win);
                              if (!focus_window) {
                                  return false;
                              }

                              auto action_result = perform_mouse_modifier_and_window_action(
                                  this->redirect, event, focus_window);
                              if (action_result.first) {
                                  return action_result.second;
                              }
                              return false;
                          }},
                          *this->redirect.pointer->focus.window);
    }

    bool axis(axis_event const& event) override
    {
        if (event.orientation == axis_orientation::horizontal) {
            // only actions on vertical scroll
            return false;
        }

        if (!this->redirect.pointer->focus.window) {
            return false;
        }

        return std::visit(overload{[&](auto&& win) {
                              auto focus_window = get_focus_lead(win);
                              if (!focus_window) {
                                  return false;
                              }

                              auto const action_result = perform_wheel_and_window_action(
                                  this->redirect, event, focus_window);
                              if (action_result.first) {
                                  return action_result.second;
                              }
                              return false;
                          }},
                          *this->redirect.pointer->focus.window);
    }

    bool touch_down(touch_down_event const& event) override
    {
        auto seat = waylandServer()->seat();
        if (seat->touches().is_in_progress()) {
            return false;
        }

        if (!this->redirect.touch->focus.window) {
            return false;
        }

        return std::visit(
            overload{[&](auto&& win) {
                auto focus_window = get_focus_lead(win);
                if (!focus_window) {
                    return false;
                }
                bool wasAction = false;
                auto const command
                    = win::get_mouse_command(focus_window, Qt::LeftButton, &wasAction);
                if (wasAction) {
                    return !win::perform_mouse_command(*focus_window, command, event.pos.toPoint());
                }
                return false;
            }},
            *this->redirect.touch->focus.window);
    }

private:
    template<typename Win>
    Win* get_focus_lead(Win* focus)
    {
        focus = win::lead_of_annexed_transient(focus);
        if (!focus->control) {
            return nullptr;
        }
        return focus;
    }
};

}
