/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "qpainter_backend.h"

#include "output.h"
#include "platform.h"
#include "qpainter_output.h"
#include "wlr_includes.h"

#include "wayland_logging.h"

namespace KWin::render::backend::wlroots
{

std::unique_ptr<qpainter_output>& get_output(base::output& output)
{
    auto&& wayland_output = static_cast<base::wayland::output&&>(output);
    auto& backend_output = static_cast<wlroots::output&>(*wayland_output.render);
    return backend_output.qpainter;
}

qpainter_backend::qpainter_backend(wlroots::platform& platform)
    : qpainter::backend()
    , platform{platform}
{
    for (auto& out : platform.base.all_outputs) {
        auto render = static_cast<output*>(static_cast<base::wayland::output*>(out)->render.get());
        get_output(*out) = std::make_unique<qpainter_output>(*render, *this);
    }
}

qpainter_backend::~qpainter_backend()
{
    tear_down();
}

void qpainter_backend::tear_down()
{
}

void qpainter_backend::begin_render(base::output& output)
{
    get_output(output)->begin_render();
}

void qpainter_backend::present(base::output* output, QRegion const& damage)
{
    wlr_renderer_end(platform.renderer);
    get_output(*output)->present(damage);
}

QImage* qpainter_backend::bufferForScreen(base::output* output)
{
    return get_output(*output)->buffer.get();
}

bool qpainter_backend::needsFullRepaint() const
{
    return false;
}

}
