/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "xwl_window.h"

#include "scene.h"
#include "win/x11/scene.h"

#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

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
    auto update_buffer_helper = [](auto window, auto& target) { update_buffer(*window, target); };

    render->update_wayland_buffer = update_buffer_helper;

    if (surface()) {
        setup_scale_scene_notify(*this);
    }
}

}
