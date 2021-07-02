/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "output.h"

#include "backend.h"
#include "buffer.h"
#include "composite.h"
#include "egl_backend.h"
#include "render/wayland/output.h"
#include "screens.h"

#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

void handle_destroy(wl_listener* listener, [[maybe_unused]] void* data)
{
    event_receiver<output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto output = event_receiver_struct->receiver;

    output->native = nullptr;
    delete output;
}

void handle_present(wl_listener* listener, [[maybe_unused]] void* data)
{
    event_receiver<output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto our_output = event_receiver_struct->receiver;
    auto event = static_cast<wlr_output_event_present*>(data);

    if (auto compositor = static_cast<WaylandCompositor*>(Compositor::self())) {
        compositor->swapped(our_output, event->when->tv_sec, event->when->tv_nsec / 1000);
    }
}

bool output::enable_native(bool enable)
{
    wlr_output_enable(native, enable);

    if (!wlr_output_test(native)) {
        qCWarning(KWIN_WL) << "Failed test commit on" << (enable ? "enabling." : "disabling.");
        // Failed test commit. Switch enabling back.
        wlr_output_enable(native, !enable);
        return false;
    }

    if (enable) {
        Compositor::self()->addRepaint(geometry());
    } else {
        auto compositor = static_cast<WaylandCompositor*>(Compositor::self());
        auto render_output = compositor->outputs.at(this).get();
        render_output->delay_timer.stop();
        wlr_output_commit(native);
    }
    return true;
}

void output::updateEnablement(bool enable)
{
    enable_native(enable);
    back->enableOutput(this, enable);
}

void output::updateDpms(DpmsMode mode)
{
    auto set_on = mode == DpmsMode::On;

    if (!enable_native(set_on)) {
        return;
    }

    if (set_on) {
        dpmsSetOn();
    } else {
        dpmsSetOff(mode);
    }
}

void output::updateMode(int modeIndex)
{
    // TODO(romangg): Determine target mode more precisly with semantic properties instead of index.
    wlr_output_mode* wlr_mode;
    auto count = 0;

    auto old_mode = native->current_mode;
    wl_list_for_each(wlr_mode, &native->modes, link)
    {
        if (count == modeIndex) {
            wlr_output_set_mode(native, wlr_mode);
            if (wlr_output_test(native)) {
                Compositor::self()->addRepaint(geometry());
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

wl_output_transform to_wl_transform(output::Transform tr)
{
    return static_cast<wl_output_transform>(tr);
}

void output::updateTransform(Transform transform)
{
    auto old_transform = native->transform;
    wlr_output_set_transform(native, to_wl_transform(transform));

    if (wlr_output_test(native)) {
        Compositor::self()->addRepaint(geometry());
    } else {
        qCWarning(KWIN_WL) << "Failed test commit on update transform call.";
        // Set previous transform.
        wlr_output_set_transform(native, old_transform);
    }
}

int output::gammaRampSize() const
{
    return wlr_output_get_gamma_size(native);
}
bool output::setGammaRamp(GammaRamp const& gamma)
{
    wlr_output_set_gamma(native, gamma.size(), gamma.red(), gamma.green(), gamma.blue());

    if (wlr_output_test(native)) {
        Compositor::self()->addRepaint(geometry());
        return true;
    } else {
        qCWarning(KWIN_WL) << "Failed test commit on set gamma ramp call.";
        // TODO(romangg): Set previous gamma.
        return false;
    }
}

output::output(wlr_output* wlr_out, backend* backend)
    : AbstractWaylandOutput()
    , native{wlr_out}
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

    auto add_mode = [&modes](int id, int width, int height, int refresh) {
        Wrapland::Server::Output::Mode mode;
        mode.id = id;
        mode.size = QSize(width, height);

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

    initInterfaces(wlr_out->name,
                   wlr_out->make,
                   wlr_out->model,
                   wlr_out->serial,
                   QSize(wlr_out->phys_width, wlr_out->phys_height),
                   modes);
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
        back->updateOutputsOn();
        Screens::self()->updateAll();
    }
}

}
