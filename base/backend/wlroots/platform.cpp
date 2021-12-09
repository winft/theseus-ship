/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "output.h"

#include <Wrapland/Server/display.h>

namespace KWin::base::backend::wlroots
{

static void handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    event_receiver<platform>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto wlr = event_receiver_struct->receiver;

    wlr->backend = nullptr;
}

platform::platform(Wrapland::Server::Display* display)
    : platform(wlr_backend_autocreate(display->native()))
{
}

platform::platform(wlr_backend* backend)
    : destroyed{std::make_unique<event_receiver<platform>>()}
{
    // TODO(romangg): Make this dependent on KWIN_WL debug verbosity.
    wlr_log_init(WLR_DEBUG, nullptr);

    this->backend = backend;

    destroyed->receiver = this;
    destroyed->event.notify = handle_destroy;
    wl_signal_add(&backend->events.destroy, &destroyed->event);
}

platform::platform(platform&& other) noexcept
{
    *this = std::move(other);
}

platform& platform::operator=(platform&& other) noexcept
{
    backend = other.backend;
    destroyed = std::move(other.destroyed);
    destroyed->receiver = this;
    other.backend = nullptr;
    return *this;
}

platform::~platform()
{
    for (auto output : all_outputs) {
        static_cast<wlroots::output*>(output)->platform = nullptr;
        delete output;
    }
    if (backend) {
        wlr_backend_destroy(backend);
    }
}

}
