/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/options.h"
#include "base/wayland/server.h"
#include "input/event.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/xkb/helpers.h"
#include "win/input.h"

#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <QWheelEvent>

namespace KWin
{

namespace input
{

enum class MouseAction {
    ModifierOnly,
    ModifierAndWindow,
};

template<typename Redirect>
bool get_modifier_command(Redirect& redirect, uint32_t key, win::mouse_cmd& command)
{
    if (xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(redirect.platform)
        != redirect.platform.base.space->options->qobject->commandAllModifier()) {
        return false;
    }
    if (redirect.pointer->isConstrained()) {
        return false;
    }
    if (redirect.space.global_shortcuts_disabled) {
        return false;
    }
    auto qt_key = button_to_qt_mouse_button(key);
    switch (qt_key) {
    case Qt::LeftButton:
        command = redirect.platform.base.space->options->qobject->commandAll1();
        break;
    case Qt::MiddleButton:
        command = redirect.platform.base.space->options->qobject->commandAll2();
        break;
    case Qt::RightButton:
        command = redirect.platform.base.space->options->qobject->commandAll3();
        break;
    default:
        // nothing
        break;
    }
    return true;
}

template<typename Redirect, typename Window>
std::pair<bool, bool>
do_perform_mouse_action(Redirect& redirect, win::mouse_cmd command, Window* window)
{
    return std::make_pair(
        true, !win::perform_mouse_command(*window, command, redirect.pointer->pos().toPoint()));
}

template<typename Redirect, typename Window>
std::pair<bool, bool>
perform_mouse_modifier_action(Redirect& redirect, button_event const& event, Window* window)
{
    auto command = win::mouse_cmd::nothing;
    auto was_action = get_modifier_command(redirect, event.key, command);

    return was_action ? do_perform_mouse_action(redirect, command, window)
                      : std::make_pair(false, false);
}

template<typename Redirect, typename Window>
std::pair<bool, bool> perform_mouse_modifier_and_window_action(Redirect& redirect,
                                                               button_event const& event,
                                                               Window* window)
{
    auto command = win::mouse_cmd::nothing;
    auto was_action = get_modifier_command(redirect, event.key, command);

    if (!was_action) {
        command = win::get_mouse_command(window, button_to_qt_mouse_button(event.key), &was_action);
    }

    return was_action ? do_perform_mouse_action(redirect, command, window)
                      : std::make_pair(false, false);
}

template<typename Redirect>
bool get_wheel_modifier_command(Redirect& redirect,
                                axis_orientation orientation,
                                double delta,
                                win::mouse_cmd& command)
{
    if (xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(redirect.platform)
        != redirect.platform.base.space->options->qobject->commandAllModifier()) {
        return false;
    }
    if (redirect.pointer->isConstrained()) {
        return false;
    }
    if (redirect.space.global_shortcuts_disabled) {
        return false;
    }

    auto veritcal_delta = (orientation == axis_orientation::vertical) ? -1 * delta : 0;
    command = redirect.platform.base.space->options->operationWindowMouseWheel(veritcal_delta);

    return true;
}

template<typename Redirect, typename Window>
std::pair<bool, bool>
perform_wheel_action(Redirect& redirect, axis_event const& event, Window* window)
{
    auto command = win::mouse_cmd::nothing;
    auto was_action = get_wheel_modifier_command(redirect, event.orientation, event.delta, command);

    return was_action ? do_perform_mouse_action(redirect, command, window)
                      : std::make_pair(false, false);
}

template<typename Redirect, typename Window>
std::pair<bool, bool>
perform_wheel_and_window_action(Redirect& redirect, axis_event const& event, Window* window)
{
    auto command = win::mouse_cmd::nothing;
    auto was_action = get_wheel_modifier_command(redirect, event.orientation, event.delta, command);

    if (!was_action) {
        command = win::get_wheel_command(window, Qt::Vertical, &was_action);
    }

    return was_action ? do_perform_mouse_action(redirect, command, window)
                      : std::make_pair(false, false);
}

template<typename Redirect>
void pass_to_wayland_server(Redirect& redirect, key_event const& event)
{
    auto seat = redirect.platform.base.server->seat();
    seat->keyboards().set_keymap(event.base.dev->xkb->keymap->cache);
    seat->keyboards().key(event.keycode,
                          event.state == key_state::pressed
                              ? Wrapland::Server::key_state::pressed
                              : Wrapland::Server::key_state::released);
}

}
}
