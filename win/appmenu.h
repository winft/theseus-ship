/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "toplevel.h"

#include <string>

namespace KWin::win
{

struct appmenu_address {
    appmenu_address() = default;
    appmenu_address(std::string name, std::string path)
        : name{name}
        , path{path}
    {
    }

    bool operator==(appmenu_address const& other) const
    {
        return name == other.name && path == other.path;
    }
    bool empty() const
    {
        return name.empty() && path.empty();
    }

    std::string name;
    std::string path;
};

struct appmenu {
    bool active{false};
    appmenu_address address;
};

template<typename Space>
Toplevel* find_window_with_appmenu(Space const& space, appmenu_address const& address)
{
    for (auto win : space.m_windows) {
        if (win->control && win->control->application_menu().address == address) {
            return win;
        }
    }
    return nullptr;
}

/**
 * Request showing the application menu bar.
 * @param actionId The DBus menu ID of the action that should be highlighted, 0 for the root menu.
 */
template<typename Win>
void show_appmenu(Win& win, int actionId)
{
    if (auto decoration = win.control->deco().decoration) {
        decoration->showApplicationMenu(actionId);
    } else {
        // No info where application menu button is, show it in the top left corner by default.
        win.space.showApplicationMenu(QRect(), &win, actionId);
    }
}

}
