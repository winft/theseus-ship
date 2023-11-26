/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"

#include <Wrapland/Server/drm_lease_v1.h>

struct wlr_output;

namespace KWin::base::backend::wlroots
{

class drm_lease;

template<typename Output>
static void non_desktop_output_handle_destroy(wl_listener* listener, void* /*data*/)
{
    base::event_receiver<Output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto output = event_receiver_struct->receiver;

    output->native = nullptr;
    delete output;
}

class non_desktop_output_wrap
{
public:
    non_desktop_output_wrap(wlr_output* wlr_out)
        : native{wlr_out}
    {
    }

    drm_lease* lease{nullptr};
    wlr_output* native;
};

template<typename Platform>
class non_desktop_output : public non_desktop_output_wrap
{
public:
    using type = non_desktop_output<Platform>;
    non_desktop_output(wlr_output* wlr_out, Platform* platform)
        : non_desktop_output_wrap(wlr_out)
        , platform{platform}
    {
        wlr_out->data = this;

        destroy_rec.receiver = this;
        destroy_rec.event.notify = non_desktop_output_handle_destroy<type>;
        wl_signal_add(&wlr_out->events.destroy, &destroy_rec.event);

        create_lease_connector();
    }

    ~non_desktop_output()
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

    Platform* platform;

private:
    void create_lease_connector()
    {
        auto lease_device = platform->frontend->drm_lease_device.get();
        if (!lease_device) {
            return;
        }

        lease_connector.reset(lease_device->create_connector(
            native->name, native->description, wlr_drm_connector_get_id(native)));
    }

    std::unique_ptr<Wrapland::Server::drm_lease_connector_v1> lease_connector;
    base::event_receiver<non_desktop_output> destroy_rec;
};

}
