/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/scene.h"
#include <base/platform_qobject.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/wl_output.h>
#include <cassert>

namespace KWin::win::wayland
{

template<typename Win>
void update_surface_outputs(Win* win)
{
    std::vector<Wrapland::Server::output*> surface_outputs;

    for (auto output : win->space.base.server->output_manager->outputs) {
        if (win->geo.frame.intersects(output->get_state().geometry.toRect())) {
            surface_outputs.push_back(output);
        }
    }

    win->surface->setOutputs(surface_outputs);
}

template<typename Win>
void set_surface(Win* win, Wrapland::Server::Surface* surface)
{
    static_assert(!Win::is_toplevel);
    assert(surface);

    if (win->surface) {
        // This can happen with XWayland clients since receiving the surface destroy signal through
        // the Wayland connection is independent of when the corresponding X11 unmap/map events
        // are received.
        QObject::disconnect(win->surface, nullptr, win->qobject.get(), nullptr);
        QObject::disconnect(win->notifiers.frame_update_outputs);
        QObject::disconnect(win->notifiers.screens_update_outputs);
    } else {
        // Need to setup this connections since setSurface was never called before or
        // the surface had been destroyed before what disconnected them.
        win->notifiers.frame_update_outputs = QObject::connect(
            win->qobject.get(), &Win::qobject_t::frame_geometry_changed, win->qobject.get(), [win] {
                update_surface_outputs(win);
            });
        win->notifiers.screens_update_outputs
            = QObject::connect(win->space.base.qobject.get(),
                               &base::platform_qobject::topology_changed,
                               win->qobject.get(),
                               [win] { update_surface_outputs(win); });
    }

    win->surface = surface;

    QObject::connect(
        win->surface, &Wrapland::Server::Surface::subsurfaceTreeChanged, win->qobject.get(), [win] {
            // TODO improve to only update actual visual area
            if (win->render_data.ready_for_painting) {
                add_full_damage(*win);
                win->render_data.is_damaged = true;
            }
        });
    QObject::connect(
        win->surface, &Wrapland::Server::Surface::destroyed, win->qobject.get(), [win] {
            win->surface = nullptr;
            win->surface_id = 0;
            QObject::disconnect(win->notifiers.frame_update_outputs);
            QObject::disconnect(win->notifiers.screens_update_outputs);
        });

    win->surface_id = surface->id();
    update_surface_outputs(win);
    Q_EMIT win->qobject->surfaceChanged();
}

}
