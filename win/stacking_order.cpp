/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "stacking_order.h"

#include "screen_edges.h"
#include "space.h"
#include "stacking.h"
#include "transient.h"
#include "x11/group.h"
#include "x11/hide.h"
#include "x11/netinfo.h"
#include "x11/stacking.h"
#include "x11/stacking_tree.h"
#include "x11/window.h"

#include "toplevel.h"

#include <algorithm>
#include <vector>

namespace KWin::win
{

stacking_order::stacking_order(win::space& space)
    : space{space}
{
}

void stacking_order::update(bool propagate_new_clients)
{
    if (block_stacking_updates > 0) {
        if (propagate_new_clients) {
            blocked_propagating_new_clients = true;
        }
        return;
    }

    auto order_changed = sort() || restacking_required;
    restacking_required = false;

    if (order_changed || propagate_new_clients) {
        x11::propagate_clients(*this, propagate_new_clients);
        render_restack_required = true;
        Q_EMIT changed();
    }
}

template<typename Win>
bool needs_child_restack(Win const& lead, Win const& child)
{
    // Tells if a transient child should be restacked directly above its lead.
    if (lead.layer() < child.layer()) {
        // Child will be in a layer above the lead and should not be pulled down from that.
        return false;
    }
    if (child.remnant) {
        return keep_deleted_transient_above(&lead, &child);
    }
    return keep_transient_above(&lead, &child);
}

void append_children(stacking_order& order, Toplevel* window, std::deque<Toplevel*>& list)
{
    auto const children = window->transient()->children;
    if (!children.size()) {
        return;
    }

    auto stacked_next = ensure_stacking_order_in_list(order.stack, children);
    std::deque<Toplevel*> stacked;

    // Append children by one first-level child after the other but between them any
    // transient children of each first-level child (acts recursively).
    for (auto child : stacked_next) {
        // Transients to multiple leads are pushed to the very end.
        if (!needs_child_restack(*window, *child)) {
            continue;
        }
        remove_all(list, child);

        stacked.push_back(child);
        append_children(order, child, stacked);
    }

    list.insert(list.end(), stacked.begin(), stacked.end());
}

/**
 * Returns a stacking order based upon \a list that fulfills certain contained.
 */
bool stacking_order::sort()
{
    std::vector<Toplevel*> pre_order = sort_windows_by_layer(pre_stack);
    std::deque<Toplevel*> stack;

    for (auto const& window : pre_order) {
        if (auto const leads = window->transient()->leads();
            std::find_if(leads.cbegin(),
                         leads.cend(),
                         [window](auto lead) { return needs_child_restack(*lead, *window); })
            != leads.cend()) {
            // Transient children that must be pushed above at least one of its leads are inserted
            // with append_children.
            continue;
        }

        assert(!contains(stack, window));
        stack.push_back(window);
        append_children(*this, window, stack);
    }

    auto order_changed = this->stack != stack;
    this->stack = stack;
    return order_changed;
}

}
