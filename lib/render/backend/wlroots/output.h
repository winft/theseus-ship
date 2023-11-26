/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_output.h"
#include "output_event.h"
#include "qpainter_output.h"
#include "wlr_includes.h"

#include "base/utils.h"
#include "base/wayland/server.h"
#include "render/wayland/output.h"
#include "render/wayland/presentation.h"

#include <chrono>
#include <stdexcept>

namespace KWin::render::backend::wlroots
{

template<typename Base, typename Platform>
class output
    : public wayland::output<typename Base::abstract_type, typename Platform::frontend_type>
{
public:
    using type = output<Base, Platform>;
    using abstract_type
        = wayland::output<typename Base::abstract_type, typename Platform::frontend_type>;
    using base_t = Base;
    using egl_output_t = egl_output<type>;
    using qpainter_output_t = qpainter_output<type>;

    output(base_t& base, Platform& platform)
        : abstract_type(base, *platform.frontend)
    {
        this->swap_pending = base.native->frame_pending;

        if (platform.egl) {
            egl = std::make_unique<egl_output_t>(*this, platform.egl->data);
        } else {
            assert(platform.qpainter);
            qpainter = std::make_unique<qpainter_output_t>(*this, platform.renderer);
        }

        present_rec.receiver = this;
        present_rec.event.notify = output_handle_present<output>;
        wl_signal_add(&base.native->events.present, &present_rec.event);

        frame_rec.receiver = this;
        frame_rec.event.notify = output_handle_frame<output>;
        wl_signal_add(&base.native->events.frame, &frame_rec.event);
    }

    std::unique_ptr<egl_output_t> egl;
    std::unique_ptr<qpainter_output_t> qpainter;

private:
    base::event_receiver<output> present_rec;
    base::event_receiver<output> frame_rec;
};

}
