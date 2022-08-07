/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "control.h"

#include <linux/input.h>

namespace KWin::input::backend::wlroots
{

bool check_alpha_numeric_keyboard(libinput_device* device)
{
    for (uint i = KEY_1; i <= KEY_0; i++) {
        if (libinput_device_keyboard_has_key(device, i) == 0) {
            return false;
        }
    }
    for (uint i = KEY_Q; i <= KEY_P; i++) {
        if (libinput_device_keyboard_has_key(device, i) == 0) {
            return false;
        }
    }
    for (uint i = KEY_A; i <= KEY_L; i++) {
        if (libinput_device_keyboard_has_key(device, i) == 0) {
            return false;
        }
    }
    for (uint i = KEY_Z; i <= KEY_M; i++) {
        if (libinput_device_keyboard_has_key(device, i) == 0) {
            return false;
        }
    }
    return true;
}

keyboard_control::keyboard_control(libinput_device* dev, KSharedConfigPtr input_config)
    : dev{dev}
{
    init_device_control(this, input_config);
    is_alpha_numeric_keyboard_cache = check_alpha_numeric_keyboard(dev);
}

bool keyboard_control::supports_disable_events() const
{
    return supports_disable_events_backend(this);
}

bool keyboard_control::is_enabled() const
{
    return is_enabled_backend(this);
}

bool keyboard_control::set_enabled_impl(bool enabled)
{
    return set_enabled_backend(this, enabled);
}

bool keyboard_control::is_alpha_numeric_keyboard() const
{
    return is_alpha_numeric_keyboard_cache;
}

void keyboard_control::update_leds(keyboard_leds leds)
{
    int libi_leds{0};
    if (flags(leds & keyboard_leds::num_lock)) {
        libi_leds = LIBINPUT_LED_NUM_LOCK;
    }
    if (flags(leds & keyboard_leds::caps_lock)) {
        libi_leds |= LIBINPUT_LED_CAPS_LOCK;
    }
    if (flags(leds & keyboard_leds::scroll_lock)) {
        libi_leds |= LIBINPUT_LED_SCROLL_LOCK;
    }
    libinput_device_led_update(dev, static_cast<libinput_led>(libi_leds));
}

}
