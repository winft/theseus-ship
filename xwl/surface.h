/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/wayland/surface.h"

#include <Wrapland/Server/surface.h>

namespace KWin::xwl
{

/// Find X11 window with the surface's id, so we may associate it with the surface.
template<typename Space>
void handle_new_surface(Space* space, Wrapland::Server::Surface* surface)
{
    if (surface->client() != space->server->xwayland_connection()) {
        // setting surface is only relevat for Xwayland clients
        return;
    }

    auto check = [surface](auto const window) {
        // Match on surface id and exclude windows already having a surface. This way we
        // only find Xwayland windows. Wayland native windows always have a surface.
        return window->surfaceId() == surface->id() && !window->surface();
    };

    if (auto window = space->findToplevel(check)) {
        win::wayland::set_surface(window, surface);
    }
}

}
