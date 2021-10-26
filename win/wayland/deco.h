/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xdg_shell.h"

namespace KWin::win::wayland
{

template<typename Server>
void handle_new_xdg_deco(Server* server, Wrapland::Server::XdgDecoration* deco)
{
    if (auto win = server->find_window(deco->toplevel()->surface()->surface())) {
        install_deco(win, deco);
    }
}

template<typename Server>
void handle_new_palette(Server* server, Wrapland::Server::ServerSideDecorationPalette* palette)
{
    if (auto win = server->find_window(palette->surface())) {
        if (win->control) {
            install_palette(win, palette);
        }
    }
}

}
