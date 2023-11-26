/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "drm_lease.h"
#include "output.h"
#include "platform_helpers.h"
#include <base/backend/wlroots/non_desktop_output.h>
#include <base/backend/wlroots/platform_events.h>
#include <base/logging.h>

#include "base/utils.h"
#include "utils/flags.h"

#include <functional>
#include <memory>

namespace Wrapland::Server
{
class Display;
}

namespace KWin::base::backend::wlroots
{

template<typename WaylandPlatform>
class platform : public WaylandPlatform
{
public:
    using type = platform<WaylandPlatform>;
    using abstract_type = WaylandPlatform;
    using output_t = wlroots::output<type>;

    using render_t = render::backend::wlroots::platform<type, typename abstract_type::render_t>;

    platform(base::config config,
             std::string const& socket_name,
             base::wayland::start_options flags,
             start_options options)
        : WaylandPlatform(std::move(config), socket_name, flags)
        , destroyed{std::make_unique<event_receiver<platform>>()}
        , new_output{std::make_unique<event_receiver<platform>>()}
    {
        align_horizontal = qgetenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL") == QByteArrayLiteral("1");

        // TODO(romangg): Make this dependent on KWIN_CORE debug verbosity.
        wlr_log_init(WLR_DEBUG, nullptr);

        if (::flags(options & start_options::headless)) {
            backend = wlr_headless_backend_create(this->server->display->native());
        } else {
#if HAVE_WLR_SESSION_ON_AUTOCREATE
            backend = wlr_backend_autocreate(this->server->display->native(), &wlroots_session);
#else
            backend = wlr_backend_autocreate(this->server->display->native());
            wlroots_session = wlr_backend_get_session(backend);
#endif
        }

        destroyed->receiver = this;
        destroyed->event.notify = handle_destroy<type>;
        wl_signal_add(&backend->events.destroy, &destroyed->event);

        new_output->receiver = this;
        new_output->event.notify = handle_new_output<type>;
        wl_signal_add(&backend->events.new_output, &new_output->event);

        if (auto drm = get_drm_backend(backend)) {
            setup_drm_leasing(this->server->display.get(), drm);
        }
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) = delete;
    platform& operator=(platform&& other) = delete;
    ~platform() override
    {
        for (auto output : this->all_outputs) {
            static_cast<output_t*>(output)->platform = nullptr;
            delete output;
        }
        for (auto output : non_desktop_outputs) {
            output->platform = nullptr;
            delete output;
        }

        if (backend) {
            wlr_backend_destroy(backend);
        }
    }

    clockid_t get_clockid() const override
    {
#if HAVE_WLR_PRESENT_CLOCK_MONOTONIC
        return CLOCK_MONOTONIC;
#else
        return wlr_backend_get_presentation_clock(backend);
#endif
    }

    std::vector<std::unique_ptr<drm_lease>> leases;
    std::vector<non_desktop_output<type>*> non_desktop_outputs;

    wlr_backend* backend{nullptr};
    wlr_session* wlroots_session{nullptr};
    bool align_horizontal{false};

private:
    void init();
    void setup_drm_leasing(Wrapland::Server::Display* display, wlr_backend* drm_backend)
    {
        this->drm_lease_device = std::make_unique<Wrapland::Server::drm_lease_device_v1>(display);

        QObject::connect(this->drm_lease_device.get(),
                         &Wrapland::Server::drm_lease_device_v1::needs_new_client_fd,
                         this,
                         [this, drm_backend] {
                             // TODO(romangg): wait in case not DRM master at the moment.
                             auto fd = wlr_drm_backend_get_non_master_fd(drm_backend);
                             this->drm_lease_device->update_fd(fd);
                         });
        QObject::connect(this->drm_lease_device.get(),
                         &Wrapland::Server::drm_lease_device_v1::leased,
                         this,
                         [this](auto lease) {
                             try {
                                 process_drm_leased(lease);
                             } catch (std::runtime_error const& e) {
                                 qCDebug(KWIN_CORE) << "Creating lease failed:" << e.what();
                                 lease->finish();

                             } catch (...) {
                                 qCWarning(KWIN_CORE)
                                     << "Creating lease failed for unknown reason.";
                                 lease->finish();
                             }
                         });
    }

    void process_drm_leased(Wrapland::Server::drm_lease_v1* lease)
    {
        std::vector<non_desktop_output_wrap*> outputs;

        qCDebug(KWIN_CORE) << "Client tries to lease DRM resources.";

        if (lease->connectors().empty()) {
            throw std::runtime_error("Lease request has no connectors specified");
        }

        for (auto& output : non_desktop_outputs) {
            for (auto& con : lease->connectors()) {
                if (wlr_drm_connector_get_id(output->native) != con->id()) {
                    continue;
                }
                if (output->lease) {
                    qCDebug(KWIN_CORE)
                        << "Failed lease," << output->native->name << "already leased";
                    lease->finish();
                    return;
                }
                outputs.push_back(output);
                break;
            }
        }

        leases.push_back(std::make_unique<drm_lease>(lease, outputs));
        auto drm_lease = leases.back().get();
        auto plat_ptr = this;

        QObject::connect(drm_lease, &drm_lease::finished, plat_ptr, [plat_ptr, drm_lease] {
            remove_all_if(plat_ptr->leases,
                          [drm_lease](auto& lease) { return lease.get() == drm_lease; });
        });

        qCDebug(KWIN_CORE) << "DRM resources have been leased to client";
    }

    std::unique_ptr<event_receiver<platform>> destroyed;
    std::unique_ptr<event_receiver<platform>> new_output;
};

}
