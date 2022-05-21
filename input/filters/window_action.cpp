/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_action.h"

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/pointer_redirect.h"
#include "input/redirect.h"
#include "input/touch_redirect.h"
#include "main.h"
#include "toplevel.h"
#include "win/input.h"
#include "win/transient.h"

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

bool window_action_filter::button(button_event const& event)
{
    if (event.state != button_state::pressed) {
        return false;
    }

    auto focus_window = get_focus_lead(kwinApp()->input->redirect->pointer()->focus.window.data());
    if (!focus_window) {
        return false;
    }

    auto action_result = perform_mouse_modifier_and_window_action(event, focus_window);
    if (action_result.first) {
        return action_result.second;
    }
    return false;
}

bool window_action_filter::axis(axis_event const& event)
{
    if (event.orientation == axis_orientation::horizontal) {
        // only actions on vertical scroll
        return false;
    }

    auto focus_window = get_focus_lead(kwinApp()->input->redirect->pointer()->focus.window.data());
    if (!focus_window) {
        return false;
    }

    auto const action_result = perform_wheel_and_window_action(event, focus_window);
    if (action_result.first) {
        return action_result.second;
    }
    return false;
}

bool window_action_filter::touch_down(touch_down_event const& event)
{
    auto seat = waylandServer()->seat();
    if (seat->touches().is_in_progress()) {
        return false;
    }
    auto focus_window = get_focus_lead(kwinApp()->input->redirect->touch()->focus.window.data());
    if (!focus_window) {
        return false;
    }
    bool wasAction = false;
    auto const command = win::get_mouse_command(focus_window, Qt::LeftButton, &wasAction);
    if (wasAction) {
        return !focus_window->performMouseCommand(command, event.pos.toPoint());
    }
    return false;
}

Toplevel* window_action_filter::get_focus_lead(Toplevel* focus)
{
    if (!focus) {
        return nullptr;
    }
    focus = win::lead_of_annexed_transient(focus);
    if (!focus->control) {
        return nullptr;
    }
    return focus;
}

}
