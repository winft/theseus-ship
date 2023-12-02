/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "base/wayland/output.h"

#include <wayland-server-core.h>

extern "C" {
#define static
#include <wlr/types/wlr_output.h>
#undef static
}

namespace KWin::base::backend::wlroots
{

template<typename Output>
void output_handle_destroy(wl_listener* listener, void* /*data*/)
{
    base::event_receiver<Output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto output = event_receiver_struct->receiver;

    output->native = nullptr;
    delete output;
}

template<typename Backend>
class output : public base::wayland::output<typename Backend::frontend_type>
{
public:
    using type = output;
    using abstract_type = base::wayland::output<typename Backend::frontend_type>;
    using render_t = typename Backend::render_t::output_t;

    output(wlr_output* wlr_out, Backend* backend)
        : abstract_type(*backend->frontend)
        , native{wlr_out}
        , backend{backend}
    {
        wlr_out->data = this;

        destroy_rec.receiver = this;
        destroy_rec.event.notify = output_handle_destroy<type>;
        wl_signal_add(&wlr_out->events.destroy, &destroy_rec.event);

        QVector<Wrapland::Server::output_mode> modes;

        Wrapland::Server::output_mode current_mode;
        if (auto wlr_mode = wlr_out->current_mode) {
            current_mode.size = QSize(wlr_mode->width, wlr_mode->height);
            current_mode.refresh_rate = wlr_mode->refresh;
        }

        auto add_mode
            = [&modes, &current_mode, &wlr_out](int id, int width, int height, int refresh) {
                  Wrapland::Server::output_mode mode;
                  mode.id = id;
                  mode.size = QSize(width, height);

                  if (wlr_out->current_mode && mode.size == current_mode.size
                      && refresh == current_mode.refresh_rate) {
                      current_mode.id = id;
                  }

                  // TODO(romangg): We fall back to 60000 here as we assume >0 in other code paths,
                  //    but in general 0 is a valid value in Wayland protocol which specifies that
                  //    the refresh rate is undefined.
                  mode.refresh_rate = refresh ? refresh : 60000;

                  modes.push_back(mode);
              };

        if (wl_list_empty(&wlr_out->modes)) {
            add_mode(0, wlr_out->width, wlr_out->height, wlr_out->refresh);
        } else {
            wlr_output_mode* wlr_mode;
            auto count = 0;
            wl_list_for_each(wlr_mode, &wlr_out->modes, link)
            {
                add_mode(count, wlr_mode->width, wlr_mode->height, wlr_mode->refresh);
                count++;
            }
        }

        auto const make = std::string(wlr_out->make ? wlr_out->make : "");
        auto const model = std::string(wlr_out->model ? wlr_out->model : "");
        auto const serial = std::string(wlr_out->serial ? wlr_out->serial : "");

        this->init_interfaces(wlr_out->name,
                              make,
                              model,
                              serial,
                              QSize(wlr_out->phys_width, wlr_out->phys_height),
                              modes,
                              current_mode.id != -1 ? &current_mode : nullptr);

        this->render = std::make_unique<render_t>(*this, backend->frontend->mod.render->backend);
    }

    ~output() override
    {
        wl_list_remove(&destroy_rec.event.link);
        if (native) {
            wlr_output_destroy(native);
        }
        if (backend) {
            remove_all(backend->frontend->outputs, this);
            remove_all(backend->frontend->all_outputs, this);
            backend->frontend->server->output_manager->commit_changes();
            Q_EMIT backend->frontend->qobject->output_removed(this);
        }
    }

    void update_dpms(base::dpms_mode mode) override
    {
        auto set_on = mode == base::dpms_mode::on;
        wlr_output_enable(native, set_on);

        if (set_on) {
            wlr_output_commit(native);
            wayland::output_set_dpms_on(*this, *backend->frontend);
            return;
        }

        if (!wlr_output_test(native)) {
            qCWarning(KWIN_CORE) << "Failed test commit on disabling output for DPMS.";
            wlr_output_enable(native, true);
            return;
        }

        get_render(this->render)->disable();
        wlr_output_commit(native);
        wayland::output_set_dmps_off(mode, *this, *backend->frontend);
    }

    bool change_backend_state(Wrapland::Server::output_state const& state) override
    {
        wlr_output_enable(native, state.enabled);

        if (state.enabled) {
            set_native_mode(native, state.mode.id);
            wlr_output_set_transform(native, static_cast<wl_output_transform>(state.transform));
            wlr_output_enable_adaptive_sync(native, state.adaptive_sync);
        }

        return wlr_output_test(native);
    }

    int gamma_ramp_size() const override
    {
        return wlr_output_get_gamma_size(native);
    }

    bool set_gamma_ramp(gamma_ramp const& gamma) override
    {
        wlr_output_set_gamma(native, gamma.size(), gamma.red(), gamma.green(), gamma.blue());

        if (!wlr_output_test(native)) {
            qCWarning(KWIN_CORE) << "Failed test commit on set gamma ramp call.";
            // TODO(romangg): Set previous gamma.
            return false;
        }
        return true;
    }

    wlr_output* native;
    Backend* backend;

private:
    base::event_receiver<output> destroy_rec;

    void set_native_mode(wlr_output* output, int mode_index)
    {
        // TODO(romangg): Determine target mode more precisly with semantic properties instead of
        // index.
        wlr_output_mode* wlr_mode;
        auto count = 0;

        wl_list_for_each(wlr_mode, &output->modes, link)
        {
            if (count == mode_index) {
                wlr_output_set_mode(output, wlr_mode);
                return;
            }
            count++;
        }
    }

    template<typename AbstractRenderOutput>
    render_t* get_render(std::unique_ptr<AbstractRenderOutput>& output)
    {
        return static_cast<render_t*>(output.get());
    }
};

}
