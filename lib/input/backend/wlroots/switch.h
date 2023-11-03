/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/switch.h"
#include <input/backend/wlroots/device_helpers.h>

#include "base/utils.h"
#include "config-kwin.h"
#include "input/platform.h"
#include "input/switch.h"

extern "C" {
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_switch.h>
}

namespace KWin::input::backend::wlroots
{

template<typename Platform>
class switch_device;

template<typename Platform>
void switch_handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    base::event_receiver<switch_device<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto switch_device = event_receiver_struct->receiver;
    platform_remove_switch(switch_device, *switch_device->platform);
    delete switch_device;
}

template<typename Platform>
void handle_toggle(struct wl_listener* listener, void* data)
{
    base::event_receiver<switch_device<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto switch_device = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_switch_toggle_event*>(data);

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

template<typename Platform>
class switch_device : public input::switch_device
{
public:
    switch_device(wlr_input_device* dev, Platform* platform)
        : platform{platform}
    {
        auto backend = wlr_switch_from_input_device(dev);

        if (auto libinput = get_libinput_device(dev)) {
            control = std::make_unique<switch_control>(libinput, platform->config.main);
        }

        destroyed.receiver = this;
        destroyed.event.notify = switch_handle_destroy<Platform>;
        wl_signal_add(&dev->events.destroy, &destroyed.event);

        toggle_rec.receiver = this;
        toggle_rec.event.notify = handle_toggle<Platform>;
        wl_signal_add(&backend->events.toggle, &toggle_rec.event);
    }

    switch_device(switch_device const&) = delete;
    switch_device& operator=(switch_device const&) = delete;
    ~switch_device() override = default;

    Platform* platform;

private:
    using er = base::event_receiver<switch_device<Platform>>;
    er destroyed;
    er toggle_rec;
};

}
