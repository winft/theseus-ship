/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <input/event.h>
#include <input/redirect.h>
#include <main.h>

namespace KWin::input::backend::x11
{

inline void pointer_button_pressed(uint32_t button, uint32_t time)
{
    if (auto input = kwinApp()->input->redirect.get()) {
        input->processPointerButton(button, button_state::pressed, time);
    }
}

inline void pointer_button_released(uint32_t button, uint32_t time)
{
    if (auto input = kwinApp()->input->redirect.get()) {
        input->processPointerButton(button, button_state::released, time);
    }
}

inline void pointer_axis_horizontal(double delta, uint32_t time, int32_t discreteDelta = 0)
{
    if (auto input = kwinApp()->input->redirect.get()) {
        input->processPointerAxis(
            axis_orientation::horizontal, delta, discreteDelta, axis_source::unknown, time);
    }
}

inline void pointer_axis_vertical(double delta, uint32_t time, int32_t discreteDelta = 0)
{
    if (auto input = kwinApp()->input->redirect.get()) {
        input->processPointerAxis(
            axis_orientation::vertical, delta, discreteDelta, axis_source::unknown, time);
    }
}

inline void keyboard_key_pressed(uint32_t key, uint32_t time)
{
    if (auto input = kwinApp()->input->redirect.get()) {
        input->processKeyboardKey(key, key_state::pressed, time);
    }
}

inline void keyboard_key_released(uint32_t key, uint32_t time)
{
    if (auto input = kwinApp()->input->redirect.get()) {
        input->processKeyboardKey(key, key_state::released, time);
    }
}

}
