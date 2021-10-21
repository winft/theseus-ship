/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "input/dbus/device_manager.h"

namespace KWin::input::wayland
{

void add_dbus(input::platform* platform)
{
    platform->dbus = std::make_unique<dbus::device_manager>(platform);
}

}
