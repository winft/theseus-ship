/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"
#include "platform.h"
#include "qpainter_output.h"
#include "wlr_includes.h"

#include "render/qpainter/backend.h"
#include "wayland_logging.h"

namespace KWin::render::backend::wlroots
{

static std::unique_ptr<qpainter_output>& get_qpainter_output(base::output& output)
{
    auto&& wayland_output = static_cast<base::wayland::output&&>(output);
    auto& backend_output = static_cast<wlroots::output&>(*wayland_output.render);
    return backend_output.qpainter;
}

class qpainter_backend : public qpainter::backend
{
public:
    qpainter_backend(wlroots::platform& platform)
        : qpainter::backend()
        , platform{platform}
    {
        for (auto& out : platform.base.all_outputs) {
            auto render
                = static_cast<output*>(static_cast<base::wayland::output*>(out)->render.get());
            get_qpainter_output(*out) = std::make_unique<qpainter_output>(*render, *this);
        }
    }

    ~qpainter_backend() override
    {
        tear_down();
    }

    void begin_render(base::output& output) override
    {
        get_qpainter_output(output)->begin_render();
    }

    void present(base::output* output, QRegion const& damage) override
    {
        wlr_renderer_end(platform.renderer);
        get_qpainter_output(*output)->present(damage);
    }

    QImage* bufferForScreen(base::output* output) override
    {
        return get_qpainter_output(*output)->buffer.get();
    }

    bool needsFullRepaint() const override
    {
        return false;
    }

    void tear_down()
    {
    }

    wlroots::platform& platform;
};

}
