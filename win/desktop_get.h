/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "main.h"

#include <NETWM>

namespace KWin::win
{

template<typename Win>
bool on_all_desktops(Win* win)
{
    return kwinApp()->operationMode() == Application::OperationModeWaylandOnly
            || kwinApp()->operationMode() == Application::OperationModeXwayland
        // Wayland
        ? win->topo.desktops.isEmpty()
        // X11
        : win->desktop() == NET::OnAllDesktops;
}

template<typename Win>
bool on_desktop(Win* win, int d)
{
    return (kwinApp()->operationMode() == Application::OperationModeWaylandOnly
                    || kwinApp()->operationMode() == Application::OperationModeXwayland
                ? win->topo.desktops.contains(
                    win->space.virtual_desktop_manager->desktopForX11Id(d))
                : win->desktop() == d)
        || on_all_desktops(win);
}

template<typename Win>
bool on_current_desktop(Win* win)
{
    return on_desktop(win, win->space.virtual_desktop_manager->current());
}

template<typename Win>
QVector<unsigned int> x11_desktop_ids(Win* win)
{
    auto const& desks = win->topo.desktops;
    QVector<unsigned int> x11_ids;
    x11_ids.reserve(desks.count());
    std::transform(desks.constBegin(), desks.constEnd(), std::back_inserter(x11_ids), [](auto vd) {
        return vd->x11DesktopNumber();
    });
    return x11_ids;
}

template<typename Win>
QStringList desktop_ids(Win* win)
{
    auto const& desks = win->topo.desktops;
    QStringList ids;
    ids.reserve(desks.count());
    std::transform(desks.constBegin(),
                   desks.constEnd(),
                   std::back_inserter(ids),
                   [](auto const* vd) { return vd->id(); });
    return ids;
}

}
