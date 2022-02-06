/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "output.h"

#include "egl_output.h"
#include "platform.h"
#include "qpainter_output.h"
#include "wlr_includes.h"

#include "base/gamma_ramp.h"
#include "base/wayland/server.h"
#include "config-kwin.h"
#include "main.h"
#include "render/wayland/compositor.h"
#include "render/wayland/output.h"
#include "render/wayland/presentation.h"
#include "screens.h"

#include <chrono>
#include <stdexcept>
#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

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

void handle_present(wl_listener* listener, void* data)
{
    base::event_receiver<output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto output = event_receiver_struct->receiver;
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

    output->presented(pres_data);
}

void handle_frame(wl_listener* listener, void* /*data*/)
{
    base::event_receiver<output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto output = event_receiver_struct->receiver;

    output->frame();
}

output::output(base::backend::wlroots::output& base, render::platform& platform)
    : wayland::output(base, platform)
{
    swap_pending = base.native->frame_pending;

    auto& render = static_cast<wlroots::platform&>(platform);
    if (render.egl) {
        egl = std::make_unique<egl_output>(*this, render.egl.get());
        QObject::connect(
            &base, &base::backend::wlroots::output::mode_changed, this, [this] { egl->reset(); });
    } else {
        assert(render.qpainter);
        qpainter = std::make_unique<qpainter_output>(*this, *render.qpainter);
    }

    present_rec.receiver = this;
    present_rec.event.notify = handle_present;
    wl_signal_add(&base.native->events.present, &present_rec.event);

    frame_rec.receiver = this;
    frame_rec.event.notify = handle_frame;
    wl_signal_add(&base.native->events.frame, &frame_rec.event);
}

output::~output() = default;

void output::disable()
{
    delay_timer.stop();
    frame_timer.stop();
}

void output::reset()
{
    platform.compositor->addRepaint(base.geometry());
}

}
