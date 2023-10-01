/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xdg_shell.h"

namespace KWin::win::wayland
{

template<typename Space>
void handle_new_xdg_deco(Space* space, Wrapland::Server::XdgDecoration* deco)
{
    if (auto win = space->find_window(deco->toplevel()->surface()->surface())) {
        install_deco(*win, deco);
    }
}

template<typename Space>
void handle_new_palette(Space* space, Wrapland::Server::ServerSideDecorationPalette* palette)
{
    if (auto win = space->find_window(palette->surface())) {
        if (win->control) {
            install_palette(*win, palette);
        }
    }
}

template<typename Win>
QRect get_icon_geometry_for_panel(Win const& win)
{
    auto management = win.control->plasma_wayland_integration;
    if (!management || !win.space.base.server) {
        // Window management interface is only available if the surface is mapped.
        return {};
    }

    auto min_distance = INT_MAX;
    Win* candidate_panel{nullptr};
    QRect candidate_geo;

    for (auto i = management->minimizedGeometries().constBegin(),
              end = management->minimizedGeometries().constEnd();
         i != end;
         ++i) {
        auto client = win.space.find_window(i.key());
        if (!client) {
            continue;
        }
        auto const distance = QPoint(client->geo.pos() - win.geo.pos()).manhattanLength();
        if (distance < min_distance) {
            min_distance = distance;
            candidate_panel = client;
            candidate_geo = i.value();
        }
    }

    if (!candidate_panel) {
        return {};
    }

    return candidate_geo.translated(candidate_panel->geo.pos());
}

}
