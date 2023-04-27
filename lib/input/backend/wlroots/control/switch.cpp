/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "switch.h"

#include "control.h"

namespace KWin::input::backend::wlroots
{

switch_control::switch_control(libinput_device* dev, KSharedConfigPtr input_config)
    : dev{dev}
{
    init_device_control(this, input_config);
}

bool switch_control::supports_disable_events() const
{
    return supports_disable_events_backend(this);
}

bool switch_control::is_enabled() const
{
    return is_enabled_backend(this);
}

bool switch_control::set_enabled_impl(bool enabled)
{
    return set_enabled_backend(this, enabled);
}

bool switch_control::is_lid_switch() const
{
    return libinput_device_switch_has_switch(dev, LIBINPUT_SWITCH_LID);
}

bool switch_control::is_tablet_mode_switch() const
{
    return libinput_device_switch_has_switch(dev, LIBINPUT_SWITCH_TABLET_MODE);
}

}
