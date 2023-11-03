/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/wayland/surface.h"
#include "win/wayland/xwl_window.h"

#include <Wrapland/Server/surface.h>

namespace KWin::xwl
{

template<typename Win>
void set_surface(Win& win, Wrapland::Server::Surface* surface)
{
    QObject::connect(
        surface, &Wrapland::Server::Surface::committed, win.qobject.get(), [win_ptr = &win] {
            auto const& state = win_ptr->surface->state();
            if (!state.damage.isEmpty()) {
                win_ptr->handle_surface_damage(state.damage);
            }
            if (state.updates & Wrapland::Server::surface_change::size) {
                win::discard_buffer(*win_ptr);
                // Quads for Xwayland clients need for size emulation. Also seems needed for
                // unmanaged Xwayland clients (compare Kate's open-file dialog when type-forward
                // list is changing size).
                // TODO(romangg): can this be put in a less hot path?
                win::discard_shape(*win_ptr);
            }
        });

    win::wayland::set_surface(&win, surface);
}

/// Find X11 window with the surface's id, so we may associate it with the surface.
template<typename Space>
void handle_new_surface(Space* space, Wrapland::Server::Surface* surface)
{
    if (surface->client() != space->base.server->xwayland_connection()) {
        // setting surface is only relevat for Xwayland clients
        return;
    }

    for (auto win : space->windows) {
        // Match on surface id and exclude windows already having a surface. This way we only find
        // Xwayland windows. Wayland native windows always have a surface.
        if (std::visit(overload{[surface](win::wayland::xwl_window<Space>* win) {
                                    if (win->remnant || win->surface_id != surface->id()
                                        || win->surface) {
                                        return false;
                                    }
                                    set_surface(*win, surface);
                                    return true;
                                },
                                [](auto&&) { return false; }},
                       win)) {
            break;
        }
    }
}

}
