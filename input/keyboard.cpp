/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "platform.h"
#include "utils.h"

namespace KWin::input
{

keyboard::keyboard(input::platform* platform)
    : platform{platform}
{
    platform->keyboards.push_back(this);
}

keyboard::~keyboard()
{
    if (platform) {
        remove_all(platform->keyboards, this);
        Q_EMIT platform->keyboard_removed(this);
    }
}

}
