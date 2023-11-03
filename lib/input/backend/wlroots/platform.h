/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/wayland/platform.h"
#include <input/logging.h>

#include <input/backend/wlroots/keyboard.h>
#include <input/backend/wlroots/pointer.h>
#include <input/backend/wlroots/switch.h>
#include <input/backend/wlroots/touch.h>

extern "C" {
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_input_device.h>
}

namespace KWin::input::backend::wlroots
{

template<typename Platform>
void platform_handle_device(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    base::event_receiver<Platform>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto input = event_receiver_struct->receiver;

    auto device = reinterpret_cast<wlr_input_device*>(data);

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        qCDebug(KWIN_INPUT) << "Keyboard device added:" << device->name;
        platform_add_keyboard(new keyboard<Platform>(device, input), *input);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        qCDebug(KWIN_INPUT) << "Pointer device added:" << device->name;
        platform_add_pointer(new pointer<Platform>(device, input), *input);
        break;
    case WLR_INPUT_DEVICE_SWITCH:
        qCDebug(KWIN_INPUT) << "Switch device added:" << device->name;
        platform_add_switch(new switch_device<Platform>(device, input), *input);
        break;
    case WLR_INPUT_DEVICE_TOUCH:
        qCDebug(KWIN_INPUT) << "Touch device added:" << device->name;
        platform_add_touch(new touch<Platform>(device, input), *input);
        break;
    default:
        // TODO(romangg): Handle other device types.
        qCDebug(KWIN_INPUT) << "Device type unhandled.";
    }
}

template<typename Base>
class platform : public input::wayland::platform<typename Base::abstract_type>
{
public:
    platform(Base& base, wlr_backend* backend, input::config config)
        : wayland::platform<typename Base::abstract_type>(base, std::move(config))
        , backend{backend}
    {
        add_device.receiver = this;
        add_device.event.notify = platform_handle_device<platform<Base>>;

        wl_signal_add(&backend->events.new_input, &add_device.event);
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;

    wlr_backend* backend{nullptr};

private:
    base::event_receiver<platform> add_device;
};

}
