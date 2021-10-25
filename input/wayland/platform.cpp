/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "input/dbus/dbus.h"
#include "input/dbus/device_manager.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/switch.h"
#include "input/touch.h"

namespace KWin::input::wayland
{

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

void platform::start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                                  QByteArray const& cursorName)
{
    if (!redirect) {
        callback(nullptr);
        return;
    }
    redirect->startInteractiveWindowSelection(callback, cursorName);
}

void platform::start_interactive_position_selection(std::function<void(QPoint const&)> callback)
{
    if (!redirect) {
        callback(QPoint(-1, -1));
        return;
    }
    redirect->startInteractivePositionSelection(callback);
}

void add_dbus(input::platform* platform)
{
    platform->dbus = std::make_unique<dbus::device_manager>(platform);
}

}
