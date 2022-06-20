/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xdg_shell.h"

namespace KWin::win::wayland
{

template<typename Space>
void handle_new_plasma_shell_surface(Space* space, Wrapland::Server::PlasmaShellSurface* surface)
{
    if (auto win = space->find_window(surface->surface())) {
        assert(win->toplevel || win->popup || win->layer_surface);
        install_plasma_shell_surface(win, surface);
    } else {
        space->plasma_shell_surfaces << surface;
        QObject::connect(surface, &QObject::destroyed, space->qobject.get(), [space, surface] {
            space->plasma_shell_surfaces.removeOne(surface);
        });
    }
}

}
