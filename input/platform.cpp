/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "cursor.h"
#include "dbus/device_manager.h"
#include "keyboard.h"
#include "pointer.h"
#include "redirect.h"
#include "switch.h"
#include "touch.h"

namespace KWin::input
{

platform::platform()
    : xkb{input::xkb::manager(this)}
{
    qRegisterMetaType<button_state>();
    qRegisterMetaType<key_state>();
}

platform::~platform()
{
    for (auto keyboard : keyboards) {
        keyboard->platform = nullptr;
    }
    for (auto pointer : pointers) {
        pointer->platform = nullptr;
    }
    for (auto switch_device : switches) {
        switch_device->platform = nullptr;
    }
    for (auto touch : touchs) {
        touch->platform = nullptr;
    }
}

}
