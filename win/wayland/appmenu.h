/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xdg_shell.h"

namespace KWin::win::wayland
{

template<typename Server>
void handle_new_appmenu(Server* server, Wrapland::Server::Appmenu* appmenu)
{
    if (auto win = server->find_window(appmenu->surface())) {
        if (win->control) {
            // Need to check that as plasma-integration creates them blindly even for
            // xdg-shell popups.
            install_appmenu(win, appmenu);
        }
    }
}

}
