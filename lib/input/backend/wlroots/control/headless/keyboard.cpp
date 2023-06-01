/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

namespace KWin::input::backend::wlroots::headless
{

bool keyboard_control::supports_disable_events() const
{
    return data.supports_disable_events;
}

bool keyboard_control::is_enabled() const
{
    return data.is_enabled;
}

bool keyboard_control::set_enabled_impl(bool enabled)
{
    data.is_enabled = enabled;
    return true;
}

bool keyboard_control::is_alpha_numeric_keyboard() const
{
    return data.is_alpha_numeric_keyboard;
}

void keyboard_control::update_leds(keyboard_leds leds)
{
    data.leds = leds;
}

}
