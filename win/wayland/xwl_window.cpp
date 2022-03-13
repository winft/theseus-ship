/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "xwl_window.h"

#include "render/wayland/buffer.h"
#include "scene.h"
#include "win/x11/scene.h"

#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

xwl_window::xwl_window(win::space& space)
    : window(space)
{
}

qreal xwl_window::bufferScale() const
{
    return surface() ? surface()->state().scale : 1;
}

bool xwl_window::setupCompositing(bool add_full_damage)
{
    return x11::setup_compositing(*this, add_full_damage);
}

void xwl_window::add_scene_window_addon()
{
    auto setup_buffer = [this](auto& buffer) {
        auto win_integrate = std::make_unique<render::wayland::buffer_win_integration>(buffer);
        auto update_helper = [&buffer]() {
            auto& win_integrate
                = static_cast<render::wayland::buffer_win_integration&>(*buffer.win_integration);
            update_buffer(*buffer.toplevel(), win_integrate.external);
        };
        win_integrate->update = update_helper;
        buffer.win_integration = std::move(win_integrate);
    };
    auto get_viewport = [](auto window, auto /*contentsRect*/) {
        // XWayland client's geometry must be taken from their content placement since the
        // buffer size is not in sync. So we only consider an explicitly set source rectangle.
        return window->surface() ? get_scaled_source_rectangle(*window) : QRectF();
    };

    render->win_integration.setup_buffer = setup_buffer;
    render->get_wayland_viewport = get_viewport;

    if (surface()) {
        setup_scale_scene_notify(*this);
    }
}

}
