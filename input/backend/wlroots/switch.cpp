/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "switch.h"

#include "control/switch.h"
#include "platform.h"

namespace KWin::input::backend::wlroots
{

using er = base::event_receiver<switch_device>;

static void handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto switch_device = event_receiver_struct->receiver;

    switch_device->backend = nullptr;
    delete switch_device;
}

static void handle_toggle(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto switch_device = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_switch_toggle*>(data);

    auto event = switch_toggle_event{
        static_cast<switch_type>(wlr_event->switch_type),
        static_cast<switch_state>(wlr_event->switch_state),
        {
            switch_device,
            wlr_event->time_msec,
        },
    };

    Q_EMIT switch_device->toggle(event);
}

switch_device::switch_device(wlr_input_device* dev, input::platform* platform)
    : input::switch_device(platform)
{
    backend = dev->switch_device;

    if (auto libinput = get_libinput_device(dev)) {
        control = std::make_unique<switch_control>(libinput, platform);
    }

    destroyed.receiver = this;
    destroyed.event.notify = handle_destroy;
    wl_signal_add(&dev->events.destroy, &destroyed.event);

    toggle_rec.receiver = this;
    toggle_rec.event.notify = handle_toggle;
    wl_signal_add(&backend->events.toggle, &toggle_rec.event);
}

}
