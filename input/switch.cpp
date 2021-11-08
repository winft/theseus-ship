/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "switch.h"

namespace KWin::input
{

switch_device::switch_device(input::platform* platform, QObject* parent)
    : QObject(parent)
    , platform{platform}
{
}

}
