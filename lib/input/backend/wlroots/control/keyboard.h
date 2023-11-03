/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/control/keyboard.h"

#include <KSharedConfig>
#include <libinput.h>

namespace KWin::input::backend::wlroots
{

class KWIN_EXPORT keyboard_control : public input::control::keyboard
{
    Q_OBJECT

public:
    keyboard_control(libinput_device* device, KSharedConfigPtr input_config);
    ~keyboard_control() override = default;

    bool supports_disable_events() const override;
    bool is_enabled() const override;
    bool set_enabled_impl(bool enabled) override;

    bool is_alpha_numeric_keyboard() const override;
    void update_leds(keyboard_leds leds) override;

    bool is_alpha_numeric_keyboard_cache{false};
    libinput_device* dev;
};

}
