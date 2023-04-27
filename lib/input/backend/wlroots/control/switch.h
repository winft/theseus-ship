/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/control/switch.h"

#include <KSharedConfig>
#include <libinput.h>

namespace KWin::input::backend::wlroots
{

class switch_control : public input::control::switch_device
{
    Q_OBJECT
public:
    switch_control(libinput_device* device, KSharedConfigPtr input_config);
    ~switch_control() override = default;

    bool supports_disable_events() const override;

    bool is_enabled() const override;
    bool set_enabled_impl(bool enabled) override;

    bool is_lid_switch() const override;
    bool is_tablet_mode_switch() const override;

    libinput_device* dev;
};

}
