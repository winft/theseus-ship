/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "keyboard.h"
#include "pointer.h"
#include "touch.h"

namespace KWin::input
{

platform::platform(QObject* parent)
    : QObject(parent)
{
}

platform::~platform()
{
    for (auto keyboard : keyboards) {
        keyboard->plat = nullptr;
    }
    for (auto pointer : pointers) {
        pointer->plat = nullptr;
    }
    for (auto touch : touchs) {
        touch->plat = nullptr;
    }
}

}
