/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "space.h"

#include "base/platform.h"
#include "base/wayland/server.h"
#include "main.h"
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

    win->surface()->setOutputs(surface_outputs);
}

template<typename Win>
void set_surface(Win* win, Wrapland::Server::Surface* surface)
{
    assert(surface);

    if (win->m_surface) {
        // This can happen with XWayland clients since receiving the surface destroy signal through
        // the Wayland connection is independent of when the corresponding X11 unmap/map events
        // are received.
        QObject::disconnect(win->m_surface, nullptr, win, nullptr);
        QObject::disconnect(win->notifiers.frame_update_outputs);
        QObject::disconnect(win->notifiers.screens_update_outputs);
    } else {
        // Need to setup this connections since setSurface was never called before or
        // the surface had been destroyed before what disconnected them.
        win->notifiers.frame_update_outputs = QObject::connect(
            win, &Toplevel::frame_geometry_changed, win, [win] { update_surface_outputs(win); });
        win->notifiers.screens_update_outputs = QObject::connect(
            &kwinApp()->get_base(), &base::platform::topology_changed, win, [win] {
                update_surface_outputs(win);
            });
    }

    win->m_surface = surface;

    if (surface->client() == waylandServer()->xwayland_connection()) {
        QObject::connect(win->m_surface, &Wrapland::Server::Surface::committed, win, [win] {
            if (!win->m_surface->state().damage.isEmpty()) {
                win->addDamage(win->m_surface->state().damage);
            }
        });
        QObject::connect(win->m_surface, &Wrapland::Server::Surface::committed, win, [win] {
            if (win->m_surface->state().updates & Wrapland::Server::surface_change::size) {
                win->discardWindowPixmap();
                // Quads for Xwayland clients need for size emulation.
                // Also apparently needed for unmanaged Xwayland clients (compare Kate's open-file
                // dialog when type-forward list is changing size).
                // TODO(romangg): can this be put in a less hot path?
                win->discard_quads();
            }
        });
    }

    QObject::connect(win->m_surface, &Wrapland::Server::Surface::subsurfaceTreeChanged, win, [win] {
        // TODO improve to only update actual visual area
        if (win->ready_for_painting) {
            win->addDamageFull();
            win->m_isDamaged = true;
        }
    });
    QObject::connect(win->m_surface, &Wrapland::Server::Surface::destroyed, win, [win] {
        win->m_surface = nullptr;
        win->m_surfaceId = 0;
        QObject::disconnect(win->notifiers.frame_update_outputs);
        QObject::disconnect(win->notifiers.screens_update_outputs);
    });

    win->m_surfaceId = surface->id();
    update_surface_outputs(win);
    Q_EMIT win->surfaceChanged();
}

}
