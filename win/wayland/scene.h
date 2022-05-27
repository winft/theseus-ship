/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/compositor.h"
#include "render/scene.h"

#include <Wrapland/Server/surface.h>
#include <cassert>

namespace KWin::win::wayland
{

template<typename Win>
void update_buffer(Win& win, std::shared_ptr<Wrapland::Server::Buffer>& target)
{
    if (!win.surface()) {
        return;
    }

    auto& buffer = win.surface()->state().buffer;
    if (!buffer) {
        return;
    }

    if (target.get() == buffer.get()) {
        return;
    }

    target = buffer;
}

template<typename Win>
QRectF get_scaled_source_rectangle(Win& win)
{
    if (auto const rect = win.surface()->state().source_rectangle; rect.isValid()) {
        auto scale = win.bufferScale();
        return QRectF(rect.topLeft() * scale, rect.bottomRight() * scale);
    }
    return {};
}

template<typename Win>
void setup_scale_scene_notify(Win& win)
{
    assert(win.surface());

    auto scene = win.space.render.scene();

    // A change of scale won't affect the geometry in compositor co-ordinates, but will affect the
    // window quads.
    QObject::connect(win.surface(), &Wrapland::Server::Surface::committed, scene, [&, scene] {
        if (win.surface()->state().updates & Wrapland::Server::surface_change::scale) {
            scene->windowGeometryShapeChanged(&win);
        }
    });
}

}
