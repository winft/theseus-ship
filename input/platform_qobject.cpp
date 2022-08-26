/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform_qobject.h"

#include "singleton_interface.h"

namespace KWin::input
{

platform_qobject::platform_qobject(std::function<void(KGlobalAccelInterface*)> accel)
    : register_global_accel{accel}
{
    singleton_interface::platform_qobject = this;
}

platform_qobject::~platform_qobject()
{
    singleton_interface::platform_qobject = nullptr;
}

}
