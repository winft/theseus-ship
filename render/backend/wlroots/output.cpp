/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "output.h"

#include "backend.h"
#include "main.h"
#include "render/wayland/compositor.h"
#include "render/wayland/output.h"
#include "render/wayland/presentation.h"
#include "screens.h"
#include "wayland_server.h"

#include <chrono>
#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

void handle_destroy(wl_listener* listener, [[maybe_unused]] void* data)
{
    base::event_receiver<output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto output = event_receiver_struct->receiver;

    output->native = nullptr;
    delete output;
}

wayland::presentation_kinds to_presentation_kinds(uint32_t wlr_flags)
{
    wayland::presentation_kinds flags{wayland::presentation_kind::none};

    if (wlr_flags & WLR_OUTPUT_PRESENT_VSYNC) {
        flags |= wayland::presentation_kind::vsync;
    }
    if (wlr_flags & WLR_OUTPUT_PRESENT_HW_CLOCK) {
        flags |= wayland::presentation_kind::hw_clock;
    }
    if (wlr_flags & WLR_OUTPUT_PRESENT_HW_COMPLETION) {
        flags |= wayland::presentation_kind::hw_completion;
    }
    if (wlr_flags & WLR_OUTPUT_PRESENT_ZERO_COPY) {
        flags |= wayland::presentation_kind::zero_copy;
    }
    return flags;
}

void handle_present(wl_listener* listener, [[maybe_unused]] void* data)
{
    base::event_receiver<output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto our_output = event_receiver_struct->receiver;
    auto event = static_cast<wlr_output_event_present*>(data);

    // TODO(romangg): What if wee don't have a monotonic clock? For example should
    //                std::chrono::system_clock::time_point be used?
    auto when = std::chrono::seconds{event->when->tv_sec}
        + std::chrono::nanoseconds{event->when->tv_nsec};

    wayland::presentation_data pres_data{event->commit_seq,
                                         when,
                                         event->seq,
                                         std::chrono::nanoseconds(event->refresh),
                                         to_presentation_kinds(event->flags)};

    if (auto compositor = static_cast<wayland::compositor*>(compositor::self())) {
        auto render_output = compositor->outputs.at(our_output).get();
        render_output->swapped(pres_data);
    }
}

void output::create_lease_connector()
{
    auto lease_device = waylandServer()->drm_lease_device();
    if (!lease_device) {
        return;
    }

    lease_connector.reset(lease_device->create_connector(base::wayland::output::wrapland_output()));
    base::wayland::output::wrapland_output()->set_connector_id(wlr_drm_connector_get_id(native));
}

bool output::disable_native()
{
    wlr_output_enable(native, false);

    if (!wlr_output_test(native)) {
        qCWarning(KWIN_WL) << "Failed test commit on disabling output.";
        // Failed test commit. Switch enabling back.
        wlr_output_enable(native, true);
        return false;
    }

    auto compositor = static_cast<wayland::compositor*>(compositor::self());
    auto render_output = compositor->outputs.at(this).get();
    render_output->delay_timer.stop();
    render_output->frame_timer.stop();
    wlr_output_commit(native);

    return true;
}

void output::update_enablement(bool enable)
{
    if (!enable) {
        disable_native();
    }
    back->enableOutput(this, enable);
    if (enable) {
        compositor::self()->addRepaint(geometry());
    }
}

void output::update_dpms(DpmsMode mode)
{
    auto set_on = mode == DpmsMode::On;

    if (set_on) {
        compositor::self()->addRepaint(geometry());
        dpms_set_on();
    } else if (disable_native()) {
        dpms_set_off(mode);
    }
}

void output::update_mode(int mode_index)
{
    // TODO(romangg): Determine target mode more precisly with semantic properties instead of index.
    wlr_output_mode* wlr_mode;
    auto count = 0;

    auto old_mode = native->current_mode;
    wl_list_for_each(wlr_mode, &native->modes, link)
    {
        if (count == mode_index) {
            wlr_output_set_mode(native, wlr_mode);
            if (wlr_output_test(native)) {
                compositor::self()->addRepaint(geometry());
            } else {
                qCWarning(KWIN_WL) << "Failed test commit on update mode call.";
                // Set previous mode.
                wlr_output_set_mode(native, old_mode);
            }
            return;
        }
        count++;
    }
}

wl_output_transform to_wl_transform(base::wayland::output_transform tr)
{
    return static_cast<wl_output_transform>(tr);
}

void output::update_transform(base::wayland::output_transform transform)
{
    auto old_transform = native->transform;
    wlr_output_set_transform(native, to_wl_transform(transform));

    if (wlr_output_test(native)) {
        compositor::self()->addRepaint(geometry());
    } else {
        qCWarning(KWIN_WL) << "Failed test commit on update transform call.";
        // Set previous transform.
        wlr_output_set_transform(native, old_transform);
    }
}

int output::gamma_ramp_size() const
{
    return wlr_output_get_gamma_size(native);
}

bool output::set_gamma_ramp(base::gamma_ramp const& gamma)
{
    wlr_output_set_gamma(native, gamma.size(), gamma.red(), gamma.green(), gamma.blue());

    if (wlr_output_test(native)) {
        compositor::self()->addRepaint(geometry());
        return true;
    } else {
        qCWarning(KWIN_WL) << "Failed test commit on set gamma ramp call.";
        // TODO(romangg): Set previous gamma.
        return false;
    }
}

output::output(wlr_output* wlr_out, backend* backend)
    : native{wlr_out}
    , back{backend}
{
    wlr_out->data = this;

    present_rec.receiver = this;
    present_rec.event.notify = handle_present;
    wl_signal_add(&wlr_out->events.present, &present_rec.event);

    destroy_rec.receiver = this;
    destroy_rec.event.notify = handle_destroy;
    wl_signal_add(&wlr_out->events.destroy, &destroy_rec.event);

    QVector<Wrapland::Server::Output::Mode> modes;

    Wrapland::Server::Output::Mode current_mode;
    if (auto wlr_mode = wlr_out->current_mode) {
        current_mode.size = QSize(wlr_mode->width, wlr_mode->height);
        current_mode.refresh_rate = wlr_mode->refresh;
    }

    auto add_mode = [&modes, &current_mode, &wlr_out](int id, int width, int height, int refresh) {
        Wrapland::Server::Output::Mode mode;
        mode.id = id;
        mode.size = QSize(width, height);

        if (wlr_out->current_mode && mode.size == current_mode.size
            && refresh == current_mode.refresh_rate) {
            current_mode.id = id;
        }

        // TODO(romangg): We fall back to 60 here as we assume >0 in other code paths, but in
        //                general 0 is a valid value in Wayland protocol which specifies that the
        //                refresh rate is undefined.
        mode.refresh_rate = refresh ? refresh : 60;

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

    init_interfaces(wlr_out->name,
                    wlr_out->make,
                    wlr_out->model,
                    wlr_out->serial,
                    QSize(wlr_out->phys_width, wlr_out->phys_height),
                    modes,
                    current_mode.id != -1 ? &current_mode : nullptr);

    create_lease_connector();
}

output::~output()
{
    wl_list_remove(&destroy_rec.event.link);
    if (native) {
        wlr_output_destroy(native);
    }
    if (back) {
        back->enabled_outputs.removeOne(this);
        back->all_outputs.removeOne(this);
        Q_EMIT back->output_removed(this);
        Screens::self()->updateAll();
    }
}

}
