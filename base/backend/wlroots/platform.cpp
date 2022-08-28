/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "non_desktop_output.h"
#include "output.h"

#include "base/singleton_interface.h"
#include "render/backend/wlroots/output.h"
#include "render/backend/wlroots/platform.h"
#include "wayland_logging.h"

#include <Wrapland/Server/display.h>
#include <stdexcept>

namespace KWin::base::backend::wlroots
{

using render_platform = render::backend::wlroots::platform<platform>;

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
    auto& render = static_cast<render_platform&>(*platform.render);
    wlr_output_init_render(native, render.allocator, render.renderer);

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

    if (align_horizontal) {
        auto shifted_geo = output->geometry();
        auto screens_width = 0;
        for (auto out : platform.outputs) {
            screens_width = std::max(out->geometry().right(), screens_width);
        }
        shifted_geo.moveLeft(screens_width);
        output->force_geometry(shifted_geo);
    }

    platform.all_outputs.push_back(output);
    platform.outputs.push_back(output);

    Q_EMIT platform.output_added(output);
}

void handle_new_output(struct wl_listener* listener, void* data)
{
    base::event_receiver<wlroots::platform>* new_output_struct
        = wl_container_of(listener, new_output_struct, event);
    auto platform = new_output_struct->receiver;
    auto native = reinterpret_cast<wlr_output*>(data);

    if (native->non_desktop) {
        platform->non_desktop_outputs.push_back(new non_desktop_output(native, platform));
        return;
    }

    try {
        add_new_output(*platform, native);
    } catch (std::runtime_error const& e) {
        qCWarning(KWIN_WL) << "Adding new output" << native->name << "failed:" << e.what();
    }
}

platform::platform(Wrapland::Server::Display* display)
    : platform(display, wlr_backend_autocreate(display->native()))
{
}

platform::platform(Wrapland::Server::Display* display, wlr_backend* backend)
    : backend{backend}
    , destroyed{std::make_unique<event_receiver<platform>>()}
    , new_output{std::make_unique<event_receiver<platform>>()}
{
    singleton_interface::platform = this;
    align_horizontal = qgetenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL") == QByteArrayLiteral("1");

    // TODO(romangg): Make this dependent on KWIN_WL debug verbosity.
    wlr_log_init(WLR_DEBUG, nullptr);

    destroyed->receiver = this;
    destroyed->event.notify = handle_destroy;
    wl_signal_add(&backend->events.destroy, &destroyed->event);

    new_output->receiver = this;
    new_output->event.notify = handle_new_output;
    wl_signal_add(&backend->events.new_output, &new_output->event);

    if (auto drm = get_drm_backend(backend)) {
        setup_drm_leasing(display, drm);
    }
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
    singleton_interface::platform = this;
    return *this;
}

platform::~platform()
{
    for (auto output : all_outputs) {
        static_cast<wlroots::output*>(output)->platform = nullptr;
        delete output;
    }
    for (auto output : non_desktop_outputs) {
        output->platform = nullptr;
        delete output;
    }
    if (backend) {
        wlr_backend_destroy(backend);
    }
    if (singleton_interface::platform == this) {
        singleton_interface::platform = nullptr;
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

void process_drm_leased(wlroots::platform& platform, Wrapland::Server::drm_lease_v1* lease)
{
    std::vector<non_desktop_output*> outputs;

    qCDebug(KWIN_WL) << "Client tries to lease DRM resources.";

    if (lease->connectors().empty()) {
        throw std::runtime_error("Lease request has no connectors specified");
    }

    for (auto& output : platform.non_desktop_outputs) {
        for (auto& con : lease->connectors()) {
            if (wlr_drm_connector_get_id(output->native) != con->id()) {
                continue;
            }
            if (output->lease) {
                qCDebug(KWIN_WL) << "Failed lease," << output->native->name << "already leased";
                lease->finish();
                return;
            }
            outputs.push_back(output);
            break;
        }
    }

    platform.leases.push_back(std::make_unique<drm_lease>(lease, outputs));
    auto drm_lease = platform.leases.back().get();
    auto plat_ptr = &platform;

    QObject::connect(drm_lease, &drm_lease::finished, plat_ptr, [plat_ptr, drm_lease] {
        remove_all_if(plat_ptr->leases,
                      [drm_lease](auto& lease) { return lease.get() == drm_lease; });
    });

    qCDebug(KWIN_WL) << "DRM resources have been leased to client";
}

void platform::setup_drm_leasing(Wrapland::Server::Display* display, wlr_backend* drm_backend)
{
    drm_lease_device = display->createDrmLeaseDeviceV1();

    connect(drm_lease_device.get(),
            &Wrapland::Server::drm_lease_device_v1::needs_new_client_fd,
            this,
            [this, drm_backend] {
                // TODO(romangg): wait in case not DRM master at the moment.
                auto fd = wlr_drm_backend_get_non_master_fd(drm_backend);
                drm_lease_device->update_fd(fd);
            });
    connect(drm_lease_device.get(),
            &Wrapland::Server::drm_lease_device_v1::leased,
            this,
            [this](auto lease) {
                try {
                    process_drm_leased(*this, lease);
                } catch (std::runtime_error const& e) {
                    qCDebug(KWIN_WL) << "Creating lease failed:" << e.what();
                    lease->finish();

                } catch (...) {
                    qCWarning(KWIN_WL) << "Creating lease failed for unknown reason.";
                    lease->finish();
                }
            });
}

}
