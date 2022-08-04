/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "switch.h"

#include "platform.h"
#include "utils/algorithm.h"

namespace KWin::input
{

switch_device::switch_device(input::platform* platform)
    : platform{platform}
{
    platform->switches.push_back(this);
}

switch_device::~switch_device()
{
    if (platform) {
        remove_all(platform->switches, this);
        Q_EMIT platform->qobject->switch_removed(this);
    }
}

}
