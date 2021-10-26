/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xdg_shell.h"

namespace KWin::win::wayland
{

template<typename Server>
void handle_new_plasma_shell_surface(Server* server, Wrapland::Server::PlasmaShellSurface* surface)
{
    if (auto win = server->find_window(surface->surface())) {
        assert(win->toplevel || win->popup || win->layer_surface);
        install_plasma_shell_surface(win, surface);
    } else {
        server->m_plasmaShellSurfaces << surface;
        QObject::connect(surface, &QObject::destroyed, server, [server, surface] {
            server->m_plasmaShellSurfaces.removeOne(surface);
        });
    }
}

}
