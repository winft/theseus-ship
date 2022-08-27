/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_output.h"
#include "output_event.h"
#include "qpainter_output.h"
#include "wlr_includes.h"

#include "base/backend/wlroots/output.h"
#include "base/utils.h"
#include "base/wayland/server.h"
#include "main.h"
#include "render/platform.h"
#include "render/wayland/compositor.h"
#include "render/wayland/output.h"
#include "render/wayland/presentation.h"

#include "wayland_logging.h"

#include <chrono>
#include <stdexcept>

namespace KWin::render::backend::wlroots
{

template<typename Platform>
class output : public wayland::output
{
public:
    using egl_output_t = egl_output<output<Platform>>;
    using qpainter_output_t = qpainter_output<output<Platform>>;

    output(base::backend::wlroots::output& base, Platform& platform)
        : wayland::output(base, platform)
    {
        swap_pending = base.native->frame_pending;

        if (platform.egl) {
            egl = std::make_unique<egl_output_t>(*this, platform.egl->data);
            QObject::connect(base.qobject.get(), &base::output_qobject::mode_changed, this, [this] {
                egl->reset();
            });
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

    void reset()
    {
        platform.compositor->addRepaint(base.geometry());
    }

    void disable()
    {
        delay_timer.stop();
        frame_timer.stop();
    }

    std::unique_ptr<egl_output_t> egl;
    std::unique_ptr<qpainter_output_t> qpainter;

private:
    base::event_receiver<output> present_rec;
    base::event_receiver<output> frame_rec;
};

}
