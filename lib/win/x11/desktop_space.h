/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "hide.h"

namespace KWin::win::x11
{

template<typename Info>
void handle_desktop_resize(Info* info, QSize const& size)
{
    if (!info) {
        return;
    }

    net::size desktop_geometry;
    desktop_geometry.width = size.width();
    desktop_geometry.height = size.height();
    info->setDesktopGeometry(desktop_geometry);
}

template<typename Space>
void popagate_subspace_change(Space& space, uint subspace)
{
    using window_t = typename Space::x11_window;

    for (auto const& var_win : space.stacking.order.stack) {
        std::visit(overload{[&](window_t* win) {
                                if (win->control && !on_subspace(*win, subspace)
                                    && var_win != space.move_resize_window) {
                                    update_visibility(win);
                                }
                            },
                            [](auto&&) {}},
                   var_win);
    }

    // Now propagate the change, after hiding, before showing.
    if (space.root_info) {
        space.root_info->setCurrentDesktop(subspaces_get_current_x11id(*space.subspace_manager));
    }

    auto const& list = space.stacking.order.stack;
    for (int i = list.size() - 1; i >= 0; --i) {
        std::visit(overload{[&](window_t* win) {
                                if (win->control && on_subspace(*win, subspace)) {
                                    update_visibility(win);
                                }
                            },
                            [](auto&&) {}},
                   list.at(i));
    }
}

template<typename Win>
bool belongs_to_desktop(Win const& win)
{
    for (auto const& member : win.group->members) {
        if (is_desktop(member)) {
            return true;
        }
    }
    return false;
}

}
