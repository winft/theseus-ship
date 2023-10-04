/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <utils/algorithm.h>
#include <win/subspace.h>
#include <win/types.h>

namespace KWin::win
{

// TODO(romangg): Is the recommendation to prefer on_subspace() still sensible?
/**
 * Returns the subspace the window is located in, 0 if it isn't located on any special subspace (not
 * mapped yet), or -1 (equals NET::OnAllDesktops). Don't use directly, use on_subspace() instead.
 */
template<typename Win>
int get_subspace(Win const& win)
{
    return win.topo.subspaces.empty() ? x11_desktop_number_on_all
                                      : win.topo.subspaces.back()->x11DesktopNumber();
}

template<typename Win>
bool on_all_subspaces(Win const& win)
{
    return win.topo.subspaces.empty();
}

template<typename Win>
bool on_subspace(Win const& win, subspace* sub)
{
    return contains(win.topo.subspaces, sub) || on_all_subspaces(win);
}

template<typename Win>
bool on_subspace(Win const& win, int sub)
{
    return on_subspace(win, win.space.subspace_manager->subspace_for_x11id(sub));
}

template<typename Win>
bool on_current_subspace(Win const& win)
{
    return on_subspace(win, win.space.subspace_manager->current);
}

template<typename Win>
QVector<unsigned int> x11_subspace_ids(Win const& win)
{
    auto const& subs = win.topo.subspaces;
    QVector<unsigned int> x11_ids;
    x11_ids.reserve(subs.size());
    std::transform(subs.cbegin(), subs.cend(), std::back_inserter(x11_ids), [](auto&& vd) {
        return vd->x11DesktopNumber();
    });
    return x11_ids;
}

template<typename Win>
QStringList subspaces_ids(Win const& win)
{
    auto const& subs = win.topo.subspaces;
    QStringList ids;
    ids.reserve(subs.size());
    std::transform(
        subs.cbegin(), subs.cend(), std::back_inserter(ids), [](auto&& vd) { return vd->id(); });
    return ids;
}

}
