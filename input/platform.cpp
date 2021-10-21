/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "cursor.h"
#include "dbus/device_manager.h"
#include "global_shortcuts_manager.h"
#include "keyboard.h"
#include "pointer.h"
#include "redirect.h"
#include "switch.h"
#include "touch.h"

namespace KWin::input
{

platform::platform()
    : QObject()
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
    for (auto switch_device : switches) {
        switch_device->plat = nullptr;
    }
    for (auto touch : touchs) {
        touch->plat = nullptr;
    }
}

void add_redirect(platform* platform, std::unique_ptr<redirect> redirect)
{
    platform->redirect = std::move(redirect);
    platform->redirect->shortcuts()->init();
}

}
