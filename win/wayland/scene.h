/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/geo.h"
#include "win/scene.h"

#include <Wrapland/Server/surface.h>
#include <cassert>

namespace KWin::win::wayland
{

template<typename Win>
void handle_surface_damage(Win& win, QRegion const& damage)
{
    assert(!damage.isEmpty());

    auto const render_region = render_geometry(&win);
    win.repaints_region += damage.translated(render_region.topLeft() - win.geo.pos());
    acquire_repaint_outputs(win, render_region);

    win.is_damaged = true;
    win.damage_region += damage;
    Q_EMIT win.qobject->damaged(damage);
}

template<typename Win>
void update_buffer(Win& win, std::shared_ptr<Wrapland::Server::Buffer>& target)
{
    if (!win.surface) {
        return;
    }

    auto& buffer = win.surface->state().buffer;
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
    if (auto const rect = win.surface->state().source_rectangle; rect.isValid()) {
        auto scale = win.bufferScale();
        return QRectF(rect.topLeft() * scale, rect.bottomRight() * scale);
    }
    return {};
}

template<typename Win>
void setup_scale_scene_notify(Win& win)
{
    assert(win.surface);

    // A change of scale won't affect the geometry in compositor co-ordinates, but will affect the
    // window quads.
    QObject::connect(
        win.surface,
        &Wrapland::Server::Surface::committed,
        win.space.base.render->compositor->scene.get(),
        [&] {
            if (win.surface->state().updates & Wrapland::Server::surface_change::scale) {
                win.space.base.render->compositor->scene->windowGeometryShapeChanged(&win);
            }
        });
}

template<typename Win>
void setup_compositing(Win& win)
{
    static_assert(!Win::is_toplevel);
    assert(!win.remnant);
    assert(win.space.base.render->compositor->scene);

    discard_shape(win);
    win.damage_region = QRect({}, win.geo.size());

    add_scene_window(*win.space.base.render->compositor->scene, win);
}

}
