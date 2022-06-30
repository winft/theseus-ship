/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stacking_tree.h"

#include "base/x11/xcb/proto.h"
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
    assert(space.stacking_order);
    QObject::connect(
        space.stacking_order.get(), &stacking_order::render_restack, &space, [this] { update(); });
}

void stacking_tree::update()
{
    if (!kwinApp()->x11Connection()) {
        return;
    }

    auto xcbtree = std::make_unique<base::x11::xcb::tree>(kwinApp()->x11RootWindow());
    if (xcbtree->is_null()) {
        return;
    }

    // this constructs a vector of references with the start and end
    // of the xcbtree C pointer array of type xcb_window_t, we use reference_wrapper to only
    // create an vector of references instead of making a copy of each element into the vector.
    std::vector<std::reference_wrapper<xcb_window_t>> windows(
        xcbtree->children(), xcbtree->children() + xcbtree->data()->children_len);
    auto const& unmanaged_list = space.unmanagedList();

    for (auto const& win : windows) {
        auto unmanaged = std::find_if(unmanaged_list.cbegin(),
                                      unmanaged_list.cend(),
                                      [&win](auto u) { return win.get() == u->xcb_window; });

        if (unmanaged != std::cend(unmanaged_list)) {
            space.stacking_order->render_overlays.push_back(*unmanaged);
        }
    }
}

}
