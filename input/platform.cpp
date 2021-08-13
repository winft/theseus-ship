/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "cursor.h"
#include "dbus/dbus.h"
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

void platform::update_keyboard_leds(input::xkb::LEDs leds)
{
    for (auto& keyboard : keyboards) {
        if (auto ctrl = keyboard->control) {
            ctrl->update_leds(leds);
        }
    }
}

void platform::toggle_touchpads()
{
    auto changed{false};
    touchpads_enabled = !touchpads_enabled;

    for (auto& pointer : pointers) {
        if (!pointer->control) {
            continue;
        }
        auto& ctrl = pointer->control;
        if (!ctrl->is_touchpad()) {
            continue;
        }

        auto old_enabled = ctrl->is_enabled();
        ctrl->set_enabled(touchpads_enabled);

        if (old_enabled != ctrl->is_enabled()) {
            changed = true;
        }
    }

    if (changed) {
        dbus::inform_touchpad_toggle(touchpads_enabled);
    }
}

void platform::enable_touchpads()
{
    if (touchpads_enabled) {
        return;
    }
    toggle_touchpads();
}

void platform::disable_touchpads()
{
    if (!touchpads_enabled) {
        return;
    }
    toggle_touchpads();
}

void add_dbus(platform* platform)
{
    platform->dbus = std::make_unique<dbus::device_manager>(platform);
}

void add_redirect(platform* platform)
{
    platform->redirect = std::make_unique<input::redirect>();
    platform->redirect->shortcuts()->init();
}

}
