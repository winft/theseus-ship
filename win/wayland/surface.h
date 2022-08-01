/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "space.h"

#include "base/platform.h"
#include "base/wayland/server.h"
#include "toplevel.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/wl_output.h>
#include <cassert>

namespace KWin::win::wayland
{

template<typename Win>
void update_surface_outputs(Win* win)
{
    std::vector<Wrapland::Server::Output*> surface_outputs;

    auto const outputs = waylandServer()->display->outputs();
    for (auto output : outputs) {
        if (win->frameGeometry().intersects(output->output()->geometry().toRect())) {
            surface_outputs.push_back(output->output());
        }
    }

    win->surface->setOutputs(surface_outputs);
}

template<typename Win>
void set_surface(Win* win, Wrapland::Server::Surface* surface)
{
    assert(surface);

    if (win->surface) {
        // This can happen with XWayland clients since receiving the surface destroy signal through
        // the Wayland connection is independent of when the corresponding X11 unmap/map events
        // are received.
        QObject::disconnect(win->surface, nullptr, win, nullptr);
        QObject::disconnect(win->notifiers.frame_update_outputs);
        QObject::disconnect(win->notifiers.screens_update_outputs);
    } else {
        // Need to setup this connections since setSurface was never called before or
        // the surface had been destroyed before what disconnected them.
        win->notifiers.frame_update_outputs = QObject::connect(
            win, &Toplevel::frame_geometry_changed, win, [win] { update_surface_outputs(win); });
        win->notifiers.screens_update_outputs
            = QObject::connect(&win->space.base, &base::platform::topology_changed, win, [win] {
                  update_surface_outputs(win);
              });
    }

    win->surface = surface;

    if (surface->client() == waylandServer()->xwayland_connection()) {
        QObject::connect(win->surface, &Wrapland::Server::Surface::committed, win, [win] {
            if (!win->surface->state().damage.isEmpty()) {
                win->addDamage(win->surface->state().damage);
            }
        });
        QObject::connect(win->surface, &Wrapland::Server::Surface::committed, win, [win] {
            if (win->surface->state().updates & Wrapland::Server::surface_change::size) {
                win->discard_buffer();
                // Quads for Xwayland clients need for size emulation.
                // Also apparently needed for unmanaged Xwayland clients (compare Kate's open-file
                // dialog when type-forward list is changing size).
                // TODO(romangg): can this be put in a less hot path?
                win->discard_quads();
            }
        });
    }

    QObject::connect(win->surface, &Wrapland::Server::Surface::subsurfaceTreeChanged, win, [win] {
        // TODO improve to only update actual visual area
        if (win->ready_for_painting) {
            win->addDamageFull();
            win->m_isDamaged = true;
        }
    });
    QObject::connect(win->surface, &Wrapland::Server::Surface::destroyed, win, [win] {
        win->surface = nullptr;
        win->surface_id = 0;
        QObject::disconnect(win->notifiers.frame_update_outputs);
        QObject::disconnect(win->notifiers.screens_update_outputs);
    });

    win->surface_id = surface->id();
    update_surface_outputs(win);
    Q_EMIT win->surfaceChanged();
}

}
