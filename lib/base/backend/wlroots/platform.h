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

extern "C" {
#define static
#include <wlr/util/log.h>
#undef static
}

namespace KWin::base::backend::wlroots
{

template<typename Frontend>
class backend
{
public:
    using type = backend<Frontend>;
    using frontend_type = Frontend;
    using output_t = wlroots::output<type>;

    using render_t = typename frontend_type::render_t::backend_t;

    backend(Frontend& frontend, start_options options)
        : frontend{&frontend}
        , destroyed{std::make_unique<event_receiver<type>>()}
        , new_output{std::make_unique<event_receiver<type>>()}
    {
        align_horizontal = qgetenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL") == QByteArrayLiteral("1");

        // TODO(romangg): Make this dependent on KWIN_CORE debug verbosity.
        wlr_log_init(WLR_DEBUG, nullptr);

        if (::flags(options & start_options::headless)) {
            native = wlr_headless_backend_create(frontend.server->display->native());
        } else {
#if HAVE_WLR_SESSION_ON_AUTOCREATE
            native = wlr_backend_autocreate(frontend.server->display->native(), &wlroots_session);
#else
            native = wlr_backend_autocreate(frontend.server->display->native());
            wlroots_session = wlr_backend_get_session(native);
#endif
        }

        destroyed->receiver = this;
        destroyed->event.notify = handle_destroy<type>;
        wl_signal_add(&native->events.destroy, &destroyed->event);

        new_output->receiver = this;
        new_output->event.notify = handle_new_output<type>;
        wl_signal_add(&native->events.new_output, &new_output->event);

        if (auto drm = get_drm_backend(native)) {
            setup_drm_leasing(frontend.server->display.get(), drm);
        }
    }

    backend(backend const&) = delete;
    backend& operator=(backend const&) = delete;
    backend(backend&& other) = delete;
    backend& operator=(backend&& other) = delete;
    virtual ~backend()
    {
        for (auto output : frontend->all_outputs) {
            static_cast<output_t*>(output)->backend = nullptr;
            delete output;
        }
        for (auto output : non_desktop_outputs) {
            output->backend = nullptr;
            delete output;
        }

        if (native) {
            wlr_backend_destroy(native);
        }
    }

    clockid_t get_clockid() const
    {
#if HAVE_WLR_PRESENT_CLOCK_MONOTONIC
        return CLOCK_MONOTONIC;
#else
        return wlr_backend_get_presentation_clock(native);
#endif
    }

    gsl::not_null<Frontend*> frontend;
    std::vector<std::unique_ptr<drm_lease>> leases;
    std::vector<non_desktop_output<type>*> non_desktop_outputs;

    wlr_backend* native{nullptr};
    wlr_session* wlroots_session{nullptr};
    bool align_horizontal{false};

private:
    void init();
    void setup_drm_leasing(Wrapland::Server::Display* display, wlr_backend* native_drm_backend)
    {
        frontend->drm_lease_device
            = std::make_unique<Wrapland::Server::drm_lease_device_v1>(display);

        QObject::connect(frontend->drm_lease_device.get(),
                         &Wrapland::Server::drm_lease_device_v1::needs_new_client_fd,
                         frontend,
                         [abs = frontend, native_drm_backend] {
                             // TODO(romangg): wait in case not DRM master at the moment.
                             auto fd = wlr_drm_backend_get_non_master_fd(native_drm_backend);
                             abs->drm_lease_device->update_fd(fd);
                         });
        QObject::connect(frontend->drm_lease_device.get(),
                         &Wrapland::Server::drm_lease_device_v1::leased,
                         frontend,
                         [this](auto lease) {
                             try {
                                 this->process_drm_leased(lease);
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

        QObject::connect(drm_lease, &drm_lease::finished, frontend, [this, drm_lease] {
            remove_all_if(leases, [drm_lease](auto& lease) { return lease.get() == drm_lease; });
        });

        qCDebug(KWIN_CORE) << "DRM resources have been leased to client";
    }

    std::unique_ptr<event_receiver<type>> destroyed;
    std::unique_ptr<event_receiver<type>> new_output;
};

}
