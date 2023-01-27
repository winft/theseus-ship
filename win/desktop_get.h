/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "main.h"

#include <NETWM>

namespace KWin::win
{

// TODO(romangg): Is the recommendation to prefer on_desktop() still sensible?
/**
 * Returns the virtual desktop the window is located in, 0 if it isn't located on any special
 * desktop (not mapped yet), or NET::OnAllDesktops. Don't use directly, use on_desktop() instead.
 */
template<typename Win>
int get_desktop(Win const& win)
{
    return win.topo.desktops.isEmpty() ? static_cast<int>(NET::OnAllDesktops)
                                       : win.topo.desktops.last()->x11DesktopNumber();
}

template<typename Win>
bool on_all_desktops(Win* win)
{
    return base::should_use_wayland_for_compositing(win->space.base.operation_mode)
        ? win->topo.desktops.isEmpty()
        : get_desktop(*win) == NET::OnAllDesktops;
}

template<typename Win>
bool on_desktop(Win* win, int d)
{
    return (base::should_use_wayland_for_compositing(win->space.base.operation_mode)
                ? win->topo.desktops.contains(
                    win->space.virtual_desktop_manager->desktopForX11Id(d))
                : get_desktop(*win) == d)
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
