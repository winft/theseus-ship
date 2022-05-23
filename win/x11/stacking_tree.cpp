/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stacking_tree.h"

#include "main.h"
#include "toplevel.h"
#include "win/internal_window.h"
#include "win/space.h"
#include "win/stacking_order.h"

namespace KWin::win::x11
{

stacking_tree::stacking_tree(win::space& space)
    : space{space}
{
}

void stacking_tree::mark_as_dirty()
{
    is_dirty = true;
    if (kwinApp()->x11Connection()) {
        xcbtree = std::make_unique<base::x11::xcb::tree>(kwinApp()->x11RootWindow());
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
    winlist = space.stacking_order->sorted();

    if (xcbtree && !xcbtree->is_null()) {
        // this constructs a vector of references with the start and end
        // of the xcbtree C pointer array of type xcb_window_t, we use reference_wrapper to only
        // create an vector of references instead of making a copy of each element into the vector.
        std::vector<std::reference_wrapper<xcb_window_t>> windows(
            xcbtree->children(), xcbtree->children() + xcbtree->data()->children_len);
        auto const& unmanaged_list = space.unmanagedList();

        for (auto const& win : windows) {
            auto unmanaged = std::find_if(unmanaged_list.begin(),
                                          unmanaged_list.end(),
                                          [&win](auto u) { return win == u->xcb_window(); });

            if (unmanaged != std::end(unmanaged_list)) {
                winlist.push_back(*unmanaged);
            }
        }

        xcbtree.reset();
    }

    for (auto const& toplevel : space.windows()) {
        auto internal = qobject_cast<internal_window*>(toplevel);
        if (internal && internal->isShown()) {
            winlist.push_back(internal);
        }
    }

    is_dirty = false;
}

}
