/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stacking_tree.h"

#include "toplevel.h"
#include "win/internal_window.h"
#include "win/stacking_order.h"
#include "workspace.h"

namespace KWin::win::x11
{

void stacking_tree::mark_as_dirty()
{
    is_dirty = true;
    if (kwinApp()->x11Connection()) {
        xcbtree.reset(new base::x11::xcb::tree(kwinApp()->x11RootWindow()));
    }
}

// Returns all windows in their stacking order on the root window.
std::deque<Toplevel*> const& stacking_tree::as_list()
{
    if (is_dirty) {
        update();
    }
    return winlist;
}

void stacking_tree::update()
{
    // use our own stacking order, not the X one, as they may differ
    winlist = workspace()->stacking_order->sorted();

    if (xcbtree && !xcbtree->is_null()) {
        std::unique_ptr<base::x11::xcb::tree> tree{std::move(xcbtree)};
        xcb_window_t* windows = tree->children();
        const auto count = tree->data()->children_len;

        auto const unmanageds = workspace()->unmanagedList();
        auto foundUnmanagedCount = unmanageds.size();
        for (size_t i = 0; i < count; ++i) {
            for (auto const& u : unmanageds) {
                if (u->xcb_window() == windows[i]) {
                    winlist.push_back(u);
                    foundUnmanagedCount--;
                    break;
                }
            }
            if (foundUnmanagedCount == 0) {
                break;
            }
        }
    }

    for (auto const& toplevel : workspace()->windows()) {
        auto internal = qobject_cast<internal_window*>(toplevel);
        if (internal && internal->isShown()) {
            winlist.push_back(internal);
        }
    }

    is_dirty = false;
}

}
