/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <win/subspace.h>

namespace KWin::win
{

template<typename Manager>
uint subspaces_get_current_x11id(Manager const& mgr)
{
    return mgr.current ? mgr.current->x11DesktopNumber() : 0;
}

template<typename Manager>
subspace* subspaces_get_for_x11id(Manager const& mgr, uint id)
{
    if (id == 0 || id > mgr.subspaces.size()) {
        return nullptr;
    }
    return mgr.subspaces.at(id - 1);
}

template<typename Manager>
subspace* subspaces_get_for_id(Manager const& mgr, QString const& id)
{
    auto desk = std::find_if(
        mgr.subspaces.begin(), mgr.subspaces.end(), [id](auto desk) { return desk->id() == id; });

    if (desk != mgr.subspaces.end()) {
        return *desk;
    }

    return nullptr;
}

template<typename Manager>
subspace& subspaces_get_north_of(Manager const& mgr, subspace& subsp, bool wrap)
{
    auto coords = mgr.grid.gridCoords(&subsp);
    assert(coords.x() >= 0);

    while (true) {
        coords.ry()--;

        if (coords.y() < 0) {
            if (!wrap) {
                // Already at the top-most subspace
                return subsp;
            }

            coords.setY(mgr.grid.height() - 1);
        }

        if (auto sp = mgr.grid.at(coords)) {
            return *sp;
        }
    }
}

template<typename Manager>
uint subspaces_get_north_of(Manager const& mgr, uint id, bool wrap)
{
    auto const subsp = subspaces_get_for_x11id(mgr, id);
    return subspaces_get_north_of(mgr, subsp ? *subsp : *mgr.current, wrap).x11DesktopNumber();
}

template<typename Manager>
subspace& subspaces_get_north_of_current(Manager const& mgr)
{
    return subspaces_get_north_of(mgr, *mgr.current, mgr.nav_wraps);
}

template<typename Manager>
subspace& subspaces_get_east_of(Manager const& mgr, subspace& subsp, bool wrap)
{
    auto coords = mgr.grid.gridCoords(&subsp);
    assert(coords.x() >= 0);

    while (true) {
        coords.rx()++;
        if (coords.x() >= mgr.grid.width()) {
            if (wrap) {
                coords.setX(0);
            } else {
                // Already at the right-most subspace
                return subsp;
            }
        }

        if (auto sp = mgr.grid.at(coords)) {
            return *sp;
        }
    }
}

template<typename Manager>
uint subspaces_get_east_of(Manager const& mgr, uint id, bool wrap)
{
    auto const subsp = subspaces_get_for_x11id(mgr, id);
    return subspaces_get_east_of(mgr, subsp ? *subsp : *mgr.current, wrap).x11DesktopNumber();
}

template<typename Manager>
subspace& subspaces_get_east_of_current(Manager const& mgr)
{
    return subspaces_get_east_of(mgr, *mgr.current, mgr.nav_wraps);
}

template<typename Manager>
subspace& subspaces_get_south_of(Manager const& mgr, subspace& subsp, bool wrap)
{
    auto coords = mgr.grid.gridCoords(&subsp);
    assert(coords.x() >= 0);

    while (true) {
        coords.ry()++;
        if (coords.y() >= mgr.grid.height()) {
            if (wrap) {
                coords.setY(0);
            } else {
                // Already at the bottom-most subspace
                return subsp;
            }
        }

        if (auto sp = mgr.grid.at(coords)) {
            return *sp;
        }
    }
}

template<typename Manager>
uint subspaces_get_south_of(Manager const& mgr, uint id, bool wrap)
{
    auto const subsp = subspaces_get_for_x11id(mgr, id);
    return subspaces_get_south_of(mgr, subsp ? *subsp : *mgr.current, wrap).x11DesktopNumber();
}

template<typename Manager>
subspace& subspaces_get_south_of_current(Manager const& mgr)
{
    return subspaces_get_south_of(mgr, *mgr.current, mgr.nav_wraps);
}

template<typename Manager>
subspace& subspaces_get_west_of(Manager const& mgr, subspace& subsp, bool wrap)
{
    auto coords = mgr.grid.gridCoords(&subsp);
    assert(coords.x() >= 0);

    while (true) {
        coords.rx()--;
        if (coords.x() < 0) {
            if (wrap) {
                coords.setX(mgr.grid.width() - 1);
            } else {
                // Already at the left-most subspace
                return subsp;
            }
        }

        if (auto sp = mgr.grid.at(coords)) {
            return *sp;
        }
    }
}

template<typename Manager>
uint subspaces_get_west_of(Manager const& mgr, uint id, bool wrap)
{
    auto const subsp = subspaces_get_for_x11id(mgr, id);
    return subspaces_get_west_of(mgr, subsp ? *subsp : *mgr.current, wrap).x11DesktopNumber();
}

template<typename Manager>
subspace& subspaces_get_west_of_current(Manager const& mgr)
{
    return subspaces_get_west_of(mgr, *mgr.current, mgr.nav_wraps);
}

template<typename Manager>
subspace& subspaces_get_successor_of(Manager const& mgr, subspace& subsp, bool wrap)
{
    auto it = std::find(mgr.subspaces.begin(), mgr.subspaces.end(), &subsp);
    assert(it != mgr.subspaces.end());
    it++;

    if (it != mgr.subspaces.end()) {
        return **it;
    }

    if (wrap) {
        return *mgr.subspaces.front();
    }

    return subsp;
}

template<typename Manager>
uint subspaces_get_successor_of(Manager const& mgr, uint id, bool wrap)
{
    auto const subsp = subspaces_get_for_x11id(mgr, id);
    return subspaces_get_successor_of(mgr, subsp ? *subsp : *mgr.current, wrap).x11DesktopNumber();
}

template<typename Manager>
subspace& subspaces_get_successor_of_current(Manager const& mgr)
{
    return subspaces_get_successor_of(mgr, *mgr.current, mgr.nav_wraps);
}

template<typename Manager>
subspace& subspaces_get_predecessor_of(Manager const& mgr, subspace& subsp, bool wrap)
{
    auto it = std::find(mgr.subspaces.begin(), mgr.subspaces.end(), &subsp);
    assert(it != mgr.subspaces.end());

    if (it != mgr.subspaces.begin()) {
        it--;
        return **it;
    }

    if (wrap) {
        return *mgr.subspaces.back();
    }

    return subsp;
}

template<typename Manager>
uint subspaces_get_predecessor_of(Manager const& mgr, uint id, bool wrap)
{
    auto const subsp = subspaces_get_for_x11id(mgr, id);
    return subspaces_get_predecessor_of(mgr, subsp ? *subsp : *mgr.current, wrap)
        .x11DesktopNumber();
}

template<typename Manager>
subspace& subspaces_get_predecessor_of_current(Manager const& mgr)
{
    return subspaces_get_predecessor_of(mgr, *mgr.current, mgr.nav_wraps);
}

}
