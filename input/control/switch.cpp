/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "switch.h"

#include "config.h"

namespace KWin::input::control
{

switch_device::switch_device()
    : device(new device_config)
{
}

}
