/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "output.h"

#include "render/backend/wlroots/output.h"
#include "render/backend/wlroots/platform.h"
#include "wayland_logging.h"

#include <Wrapland/Server/display.h>
#include <stdexcept>

namespace KWin::base::backend::wlroots
{

static auto align_horizontal{false};

static void handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    event_receiver<platform>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto wlr = event_receiver_struct->receiver;

    wlr->backend = nullptr;
}

void add_new_output(wlroots::platform& platform, wlr_output* native)
{
    auto const screens_width = std::max(platform.screens.size().width(), 0);

#if HAVE_WLR_OUTPUT_INIT_RENDER
    auto& render = static_cast<render::backend::wlroots::platform&>(*platform.render);
    wlr_output_init_render(native, render.allocator, render.renderer);
#endif

    if (!wl_list_empty(&native->modes)) {
        auto mode = wlr_output_preferred_mode(native);
        wlr_output_set_mode(native, mode);
        wlr_output_enable(native, true);
        if (!wlr_output_test(native)) {
            throw std::runtime_error("wlr_output_test failed");
        }
        if (!wlr_output_commit(native)) {
            throw std::runtime_error("wlr_output_commit failed");
        }
    }

    auto output = new wlroots::output(native, &platform);

    platform.all_outputs.push_back(output);
    platform.outputs.push_back(output);

    Q_EMIT platform.output_added(output);

    if (align_horizontal) {
        auto shifted_geo = output->geometry();
        shifted_geo.moveLeft(screens_width);
        output->force_geometry(shifted_geo);
    }

    platform.screens.updateAll();
}

void handle_new_output(struct wl_listener* listener, void* data)
{
    base::event_receiver<wlroots::platform>* new_output_struct
        = wl_container_of(listener, new_output_struct, event);
    auto platform = new_output_struct->receiver;
    auto native = reinterpret_cast<wlr_output*>(data);

    try {
        add_new_output(*platform, native);
    } catch (std::runtime_error const& e) {
        qCWarning(KWIN_WL) << "Adding new output" << native->name << "failed:" << e.what();
    }
}

platform::platform(Wrapland::Server::Display* display)
    : platform(wlr_backend_autocreate(display->native()))
{
}

platform::platform(wlr_backend* backend)
    : destroyed{std::make_unique<event_receiver<platform>>()}
    , new_output{std::make_unique<event_receiver<platform>>()}
{
    align_horizontal = qgetenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL") == QByteArrayLiteral("1");

    // TODO(romangg): Make this dependent on KWIN_WL debug verbosity.
    wlr_log_init(WLR_DEBUG, nullptr);

    this->backend = backend;

    destroyed->receiver = this;
    destroyed->event.notify = handle_destroy;
    wl_signal_add(&backend->events.destroy, &destroyed->event);

    new_output->receiver = this;
    new_output->event.notify = handle_new_output;
    wl_signal_add(&backend->events.new_output, &new_output->event);
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
    new_output = std::move(other.new_output);
    new_output->receiver = this;
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

wlr_session* platform::session() const
{
    return wlr_backend_get_session(backend);
}

clockid_t platform::get_clockid() const
{
    return wlr_backend_get_presentation_clock(backend);
}

}
