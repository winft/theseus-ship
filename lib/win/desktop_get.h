/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "virtual_desktops.h"
#include <win/types.h>

namespace KWin::win
{

// TODO(romangg): Is the recommendation to prefer on_desktop() still sensible?
/**
 * Returns the virtual desktop the window is located in, 0 if it isn't located on any special
 * desktop (not mapped yet), or -1 (equals NET::OnAllDesktops). Don't use directly, use on_desktop()
 * instead.
 */
template<typename Win>
int get_desktop(Win const& win)
{
    return win.topo.desktops.empty() ? x11_desktop_number_on_all
                                     : win.topo.desktops.back()->x11DesktopNumber();
}

template<typename Win>
bool on_all_desktops(Win const& win)
{
    return win.topo.desktops.empty();
}

template<typename Win>
bool on_desktop(Win const& win, virtual_desktop* vd)
{
    return win.topo.desktops.contains(vd) || on_all_desktops(win);
}

template<typename Win>
bool on_desktop(Win const& win, int d)
{
    return on_desktop(win, win.space.virtual_desktop_manager->desktopForX11Id(d));
}

template<typename Win>
bool on_current_desktop(Win const& win)
{
    return on_desktop(win, win.space.virtual_desktop_manager->current());
}

template<typename Win>
QVector<unsigned int> x11_desktop_ids(Win const& win)
{
    auto const& desks = win.topo.desktops;
    QVector<unsigned int> x11_ids;
    x11_ids.reserve(desks.count());
    std::transform(desks.constBegin(), desks.constEnd(), std::back_inserter(x11_ids), [](auto vd) {
        return vd->x11DesktopNumber();
    });
    return x11_ids;
}

template<typename Win>
QStringList desktop_ids(Win const& win)
{
    auto const& desks = win.topo.desktops;
    QStringList ids;
    ids.reserve(desks.count());
    std::transform(desks.constBegin(),
                   desks.constEnd(),
                   std::back_inserter(ids),
                   [](auto const* vd) { return vd->id(); });
    return ids;
}

template<typename Win>
QVector<virtual_desktop*> get_desktops(Win const& win)
{
    return win.topo.desktops;
}

}
