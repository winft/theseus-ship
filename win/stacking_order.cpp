/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "stacking_order.h"
#include "stacking.h"
#include "transient.h"

#include "win/x11/group.h"
#include "win/x11/hide.h"
#include "win/x11/netinfo.h"
#include "win/x11/stacking.h"
#include "win/x11/stacking_tree.h"
#include "win/x11/window.h"

#include "screen_edges.h"
#include "screens.h"
#include "toplevel.h"
#include "workspace.h"

namespace KWin::win
{

void stacking_order::update(bool propagate_new_clients)
{
    if (block_stacking_updates > 0) {
        if (propagate_new_clients)
            blocked_propagating_new_clients = true;
        return;
    }
    bool order_changed = sort() || restacking_required;
    restacking_required = false;
    if (order_changed || propagate_new_clients) {
        propagate_clients(propagate_new_clients);
        workspace()->x_stacking_tree->mark_as_dirty();
        Q_EMIT changed();
    }
}

/**
 * Returns a stacking order based upon \a list that fulfills certain contained.
 */
bool stacking_order::sort()
{
    std::vector<Toplevel*> pre_order = x11::sort_windows_by_layer(pre_stack);
    std::deque<Toplevel*> stack;

    auto child_restack = [](auto lead, auto child) {
        // Tells if a transient child should be restacked directly above its lead.
        if (lead->layer() < child->layer()) {
            // Child will be in a layer above the lead and should not be pulled down from that.
            return false;
        }
        if (child->remnant()) {
            return keep_deleted_transient_above(lead, child);
        }
        return keep_transient_above(lead, child);
    };

    auto append_children = [this, &child_restack](Toplevel* window, std::deque<Toplevel*>& list) {
        auto impl =
            [this, &child_restack](Toplevel* window, std::deque<Toplevel*>& list, auto& impl_ref) {
                auto const children = window->transient()->children;
                if (!children.size()) {
                    return;
                }

                auto stacked_next = ensure_stacking_order_in_list(win_stack, children);
                std::deque<Toplevel*> stacked;

                // Append children by one first-level child after the other but between them any
                // transient children of each first-level child (acts recursively).
                for (auto child : stacked_next) {
                    // Transients to multiple leads are pushed to the very end.
                    if (!child_restack(window, child)) {
                        continue;
                    }
                    remove_all(list, child);

                    stacked.push_back(child);
                    impl_ref(child, stacked, impl_ref);
                }

                list.insert(list.end(), stacked.begin(), stacked.end());
            };

        impl(window, list, impl);
    };

    for (auto const& window : pre_order) {
        if (auto const leads = window->transient()->leads();
            std::find_if(leads.cbegin(),
                         leads.cend(),
                         [window, child_restack](auto lead) { return child_restack(lead, window); })
            != leads.cend()) {
            // Transient children that must be pushed above at least one of its leads are inserted
            // with append_children.
            continue;
        }

        assert(!contains(stack, window));
        stack.push_back(window);
        append_children(window, stack);
    }

    bool order_changed = (win_stack != stack);
    win_stack = stack;
    return order_changed;
}

/**
 * Propagates the managed clients to the world.
 * Called ONLY from update_stacking_order().
 */
void stacking_order::propagate_clients(bool propagate_new_clients)
{
    if (!x11::rootInfo()) {
        return;
    }
    // restack the windows according to the stacking order
    // supportWindow > electric borders > clients > hidden clients
    std::vector<xcb_window_t> new_win_stack;

    // Stack all windows under the support window. The support window is
    // not used for anything (besides the NETWM property), and it's not shown,
    // but it was lowered after kwin startup. Stacking all clients below
    // it ensures that no client will be ever shown above override-redirect
    // windows (e.g. popups).
    new_win_stack.push_back(x11::rootInfo()->supportWindow());

    auto const edges_wins = workspace()->edges->windows();
    new_win_stack.insert(new_win_stack.end(), edges_wins.begin(), edges_wins.end());
    new_win_stack.insert(new_win_stack.end(), manual_overlays.begin(), manual_overlays.end());

    // Twice the stacking-order size for inputWindow
    new_win_stack.reserve(new_win_stack.size() + 2 * win_stack.size());

    for (int i = win_stack.size() - 1; i >= 0; --i) {
        auto x11_window = qobject_cast<x11::window*>(win_stack.at(i));
        if (!x11_window || x11::hidden_preview(x11_window)) {
            continue;
        }

        if (x11_window->xcb_windows.input) {
            // Stack the input window above the frame
            new_win_stack.push_back(x11_window->xcb_windows.input);
        }

        new_win_stack.push_back(x11_window->frameId());
    }

    // when having hidden previews, stack hidden windows below everything else
    // (as far as pure X stacking order is concerned), in order to avoid having
    // these windows that should be unmapped to interfere with other windows
    for (int i = win_stack.size() - 1; i >= 0; --i) {
        auto x11_window = qobject_cast<x11::window*>(win_stack.at(i));
        if (!x11_window || !x11::hidden_preview(x11_window)) {
            continue;
        }
        new_win_stack.push_back(x11_window->frameId());
    }
    // TODO isn't it too inefficient to restack always all clients?
    // TODO don't restack not visible windows?
    Q_ASSERT(new_win_stack.at(0) == x11::rootInfo()->supportWindow());
    base::x11::xcb::restack_windows(new_win_stack);

    int pos = 0;
    xcb_window_t* xcb_windows(nullptr);

    std::vector<x11::window*> x11_windows;
    for (auto const& window : workspace()->allClientList()) {
        auto x11_window = qobject_cast<x11::window*>(window);
        if (x11_window) {
            x11_windows.push_back(x11_window);
        }
    }

    if (propagate_new_clients) {
        xcb_windows = new xcb_window_t[manual_overlays.size() + x11_windows.size()];
        for (const auto win : manual_overlays) {
            xcb_windows[pos++] = win;
        }

        // TODO this is still not completely in the map order
        // TODO(romangg): can we make this more efficient (only looping once)?
        for (auto const& x11_window : x11_windows) {
            if (is_desktop(x11_window)) {
                xcb_windows[pos++] = x11_window->xcb_window();
            }
        }
        for (auto const& x11_window : x11_windows) {
            if (!is_desktop(x11_window)) {
                xcb_windows[pos++] = x11_window->xcb_window();
            }
        }

        x11::rootInfo()->setClientList(xcb_windows, pos);
        delete[] xcb_windows;
    }

    xcb_windows = new xcb_window_t[manual_overlays.size() + win_stack.size()];
    pos = 0;
    for (auto const& window : win_stack) {
        if (auto x11_window = qobject_cast<x11::window*>(window)) {
            xcb_windows[pos++] = x11_window->xcb_window();
        }
    }
    for (auto const& win : manual_overlays) {
        xcb_windows[pos++] = win;
    }
    x11::rootInfo()->setClientListStacking(xcb_windows, pos);
    delete[] xcb_windows;
}

}
