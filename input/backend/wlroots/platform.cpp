/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "keyboard.h"
#include "pointer.h"
#include "switch.h"
#include "touch.h"

#include "input/logging.h"

namespace KWin::input::backend::wlroots
{

void handle_device(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    base::event_receiver<platform>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto input = event_receiver_struct->receiver;

    auto device = reinterpret_cast<wlr_input_device*>(data);

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        qCDebug(KWIN_INPUT) << "Keyboard device added:" << device->name;
        input->keyboards.emplace_back(new keyboard(device, input));
        Q_EMIT input->keyboard_added(input->keyboards.back());
        break;
    case WLR_INPUT_DEVICE_POINTER:
        qCDebug(KWIN_INPUT) << "Pointer device added:" << device->name;
        input->pointers.emplace_back(new pointer(device, input));
        Q_EMIT input->pointer_added(input->pointers.back());
        break;
    case WLR_INPUT_DEVICE_SWITCH:
        qCDebug(KWIN_INPUT) << "Switch device added:" << device->name;
        input->switches.emplace_back(new switch_device(device, input));
        Q_EMIT input->switch_added(input->switches.back());
        break;
    case WLR_INPUT_DEVICE_TOUCH:
        qCDebug(KWIN_INPUT) << "Touch device added:" << device->name;
        input->touchs.emplace_back(new touch(device, input));
        Q_EMIT input->touch_added(input->touchs.back());
        break;
    default:
        // TODO(romangg): Handle other device types.
        qCDebug(KWIN_INPUT) << "Device type unhandled.";
    }
}

platform::platform(wayland_base const& base)
    : wayland::platform(base)
{
    add_device.receiver = this;
    add_device.event.notify = handle_device;
    wl_signal_add(&base.backend.backend->events.new_input, &add_device.event);
}

}
