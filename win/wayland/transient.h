/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <algorithm>

namespace KWin::win::wayland
{

template<typename Window, typename Server>
void adopt_transient_children(Server* server, Window* window)
{
    std::for_each(server->windows.cbegin(), server->windows.cend(), [&window](auto win) {
        win->checkTransient(window);
    });
}

}
