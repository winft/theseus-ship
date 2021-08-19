/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <input/event.h>

#include <QWheelEvent>

namespace KWin
{
class Toplevel;

namespace input
{

enum class MouseAction {
    ModifierOnly,
    ModifierAndWindow,
};

std::pair<bool, bool> perform_mouse_modifier_action(button_event const& event, Toplevel* window);
std::pair<bool, bool> perform_mouse_modifier_and_window_action(button_event const& event,
                                                               Toplevel* window);

std::pair<bool, bool> perform_wheel_action(axis_event const& event, Toplevel* window);
std::pair<bool, bool> perform_wheel_and_window_action(axis_event const& event, Toplevel* window);

}
}
