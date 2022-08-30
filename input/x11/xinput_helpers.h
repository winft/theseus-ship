/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "main.h"

namespace KWin::input::x11
{

inline void pointer_button_pressed(uint32_t button, uint32_t time, input::pointer* pointer)
{
    Q_EMIT pointer->button_changed({button, button_state::pressed, {pointer, time}});
}

inline void pointer_button_released(uint32_t button, uint32_t time, input::pointer* pointer)
{
    Q_EMIT pointer->button_changed({button, button_state::released, {pointer, time}});
}

inline void
pointer_axis_horizontal(double delta, uint32_t time, int32_t discreteDelta, input::pointer* pointer)
{
    Q_EMIT pointer->axis_changed({axis_source::unknown,
                                  axis_orientation::horizontal,
                                  delta,
                                  discreteDelta,
                                  {pointer, time}});
}

inline void
pointer_axis_vertical(double delta, uint32_t time, int32_t discreteDelta, input::pointer* pointer)
{
    Q_EMIT pointer->axis_changed(
        {axis_source::unknown, axis_orientation::vertical, delta, discreteDelta, {pointer, time}});
}

inline void keyboard_key_pressed(uint32_t key, uint32_t time, input::keyboard* keyboard)
{
    Q_EMIT keyboard->key_changed({key, key_state::pressed, false, {keyboard, time}});
}

inline void keyboard_key_released(uint32_t key, uint32_t time, input::keyboard* keyboard)
{
    Q_EMIT keyboard->key_changed({key, key_state::released, false, {keyboard, time}});
}

}
