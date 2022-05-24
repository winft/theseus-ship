/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "non_desktop_output.h"

#include "drm_lease.h"
#include "platform.h"

#include "base/wayland/server.h"
#include "main.h"

namespace KWin::base::backend::wlroots
{

static void handle_destroy(wl_listener* listener, void* /*data*/)
{
    base::event_receiver<non_desktop_output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto output = event_receiver_struct->receiver;

    output->native = nullptr;
    delete output;
}

non_desktop_output::non_desktop_output(wlr_output* wlr_out, wlroots::platform* platform)
    : native{wlr_out}
    , platform{platform}
{
    wlr_out->data = this;

    destroy_rec.receiver = this;
    destroy_rec.event.notify = handle_destroy;
    wl_signal_add(&wlr_out->events.destroy, &destroy_rec.event);

    create_lease_connector();
}

void non_desktop_output::create_lease_connector()
{
    auto lease_device = platform->drm_lease_device.get();
    if (!lease_device) {
        return;
    }

    lease_connector.reset(lease_device->create_connector(
        native->name, native->description, wlr_drm_connector_get_id(native)));
}

non_desktop_output::~non_desktop_output()
{
    wl_list_remove(&destroy_rec.event.link);
    if (lease) {
        remove_all(lease->outputs, this);
    }
    if (native) {
        wlr_output_destroy(native);
    }
    if (platform) {
        remove_all(platform->non_desktop_outputs, this);
    }
}

}
