/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/redirect.h"
#include "main.h"

namespace KWin::input::backend::x11
{

inline void pointer_button_pressed(uint32_t button, uint32_t time)
{
    if (auto input = kwinApp()->input_redirect.get()) {
        input->processPointerButton(button, redirect::PointerButtonPressed, time);
    }
}

inline void pointer_button_released(uint32_t button, uint32_t time)
{
    if (auto input = kwinApp()->input_redirect.get()) {
        input->processPointerButton(button, redirect::PointerButtonReleased, time);
    }
}

inline void pointer_axis_horizontal(double delta, uint32_t time, int32_t discreteDelta = 0)
{
    if (auto input = kwinApp()->input_redirect.get()) {
        input->processPointerAxis(redirect::PointerAxisHorizontal,
                                  delta,
                                  discreteDelta,
                                  redirect::PointerAxisSourceUnknown,
                                  time);
    }
}

inline void pointer_axis_vertical(double delta, uint32_t time, int32_t discreteDelta = 0)
{
    if (auto input = kwinApp()->input_redirect.get()) {
        input->processPointerAxis(redirect::PointerAxisVertical,
                                  delta,
                                  discreteDelta,
                                  redirect::PointerAxisSourceUnknown,
                                  time);
    }
}

inline void keyboard_key_pressed(uint32_t key, uint32_t time)
{
    if (auto input = kwinApp()->input_redirect.get()) {
        input->processKeyboardKey(key, redirect::KeyboardKeyPressed, time);
    }
}

inline void keyboard_key_released(uint32_t key, uint32_t time)
{
    if (auto input = kwinApp()->input_redirect.get()) {
        input->processKeyboardKey(key, redirect::KeyboardKeyReleased, time);
    }
}

}
