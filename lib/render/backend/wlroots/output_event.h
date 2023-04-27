/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include "render/wayland/presentation.h"

namespace KWin::render::backend::wlroots
{

inline wayland::presentation_kinds output_flags_to_presentation_kinds(uint32_t wlr_flags)
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

template<typename Output>
void output_handle_present(wl_listener* listener, void* data)
{
    base::event_receiver<Output>* event_receiver_struct
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
                                         output_flags_to_presentation_kinds(event->flags)};

    output->presented(pres_data);
}

template<typename Output>
void output_handle_frame(wl_listener* listener, void* /*data*/)
{
    base::event_receiver<Output>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto output = event_receiver_struct->receiver;

    output->frame();
}

}
