/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "switch.h"

#include "control/switch.h"
#include "platform.h"
#include "utils.h"

namespace KWin::input::backend::wlroots
{

using er = event_receiver<switch_device>;

static void handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto switch_device = event_receiver_struct->receiver;

    switch_device->backend = nullptr;

    if (switch_device->plat) {
        remove_all(switch_device->plat->switches, switch_device);
        Q_EMIT switch_device->plat->switch_removed(switch_device);
    }
}

static void handle_toggle(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto switch_device = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_switch_toggle*>(data);

    auto event = toggle_event{
        static_cast<switch_type>(wlr_event->switch_type),
        static_cast<switch_state>(wlr_event->switch_state),
        {
            switch_device,
            wlr_event->time_msec,
        },
    };

    Q_EMIT switch_device->toggle(event);
}

switch_device::switch_device(wlr_input_device* dev, platform* plat)
    : input::switch_device(plat)
{
    backend = dev->switch_device;

    if (auto libinput = get_libinput_device(dev)) {
        control = new switch_control(libinput, plat);
    }

    destroyed.receiver = this;
    destroyed.event.notify = handle_destroy;
    wl_signal_add(&dev->events.destroy, &destroyed.event);

    toggle_rec.receiver = this;
    toggle_rec.event.notify = handle_toggle;
    wl_signal_add(&backend->events.toggle, &toggle_rec.event);
}

}
