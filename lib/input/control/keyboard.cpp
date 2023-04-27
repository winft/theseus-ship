/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "config.h"

namespace KWin::input::control
{

keyboard::keyboard()
    : device(new device_config)
{
}

}
