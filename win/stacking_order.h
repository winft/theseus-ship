/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "net.h"
#include "stacking.h"
#include "transient_stacking.h"

#include <QObject>
#include <algorithm>
#include <deque>
#include <memory>
#include <vector>
#include <xcb/xcb.h>

namespace KWin::win
{

template<typename Order>
auto render_stack(Order& order)
{
    if (order.render_restack_required) {
        order.render_restack_required = false;
        order.render_overlays = {};
        Q_EMIT order.qobject->render_restack();
    }

    auto stack = order.stack;
    std::copy(std::begin(order.render_overlays),
              std::end(order.render_overlays),
              std::back_inserter(stack));
    return stack;
}

class KWIN_EXPORT stacking_order_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    /**
     * This signal is emitted every time the `unlock()` method is called,
     * most often because a StackingUpdatesBlocker goes out of scope and is destroyed.
     * Current consumers:
     * - EffectsHandlerImpl::checkInputWindowStacking()
     */
    void unlocked();
    void render_restack();
    /**
     * This signal is emitted when the stacking order changed, i.e. a window is risen
     * or lowered
     */
    void changed(bool window_count_changed);
};

template<typename Window>
class stacking_order
{
public:
    stacking_order()
        : qobject{std::make_unique<stacking_order_qobject>()}
    {
    }

    void update_order()
    {
        if (block_stacking_updates > 0) {
            return;
        }

        if (sort() || restacking_required) {
            process_change();
            Q_EMIT qobject->changed(false);
        }
    }

    void update_count()
    {
        if (block_stacking_updates > 0) {
            blocked_propagating_new_clients = true;
            return;
        }

        sort();
        process_change();
        Q_EMIT qobject->changed(true);
    }

    void lock()
    {
        if (block_stacking_updates == 0)
            blocked_propagating_new_clients = false;
        ++block_stacking_updates;
    }

    void unlock()
    {
        if (--block_stacking_updates == 0) {
            if (blocked_propagating_new_clients) {
                update_count();
            } else {
                update_order();
            }
            Q_EMIT qobject->unlocked();
        }
    }

    void force_restacking()
    {
        restacking_required = true;
        lock();
        unlock();
    }

    std::unique_ptr<stacking_order_qobject> qobject;

    /// How windows are configured in z-direction. Topmost window at back.
    std::deque<Window*> stack;
    std::deque<Window*> pre_stack;

    /// Windows on top of the the stack that shall be composited addtionally.
    std::deque<Window*> render_overlays;
    std::deque<xcb_window_t> manual_overlays;

    bool render_restack_required{false};

private:
    template<typename Win>
    static bool needs_child_restack(Win const& lead, Win const& child)
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

    template<typename Win>
    static void append_children(stacking_order& order, Win* window, std::deque<Win*>& list)
    {
        auto const children = window->transient()->children;
        if (!children.size()) {
            return;
        }

        auto stacked_next = ensure_stacking_order_in_list(order, children);
        std::deque<Win*> stacked;

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

    bool sort()
    {
        auto pre_order = sort_windows_by_layer(pre_stack);
        std::deque<Window*> stack;

        for (auto const& window : pre_order) {
            if (auto const leads = window->transient()->leads();
                std::find_if(leads.cbegin(),
                             leads.cend(),
                             [window](auto lead) { return needs_child_restack(*lead, *window); })
                != leads.cend()) {
                // Transient children that must be pushed above at least one of its leads are
                // inserted with append_children.
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

    void process_change()
    {
        restacking_required = false;
        render_restack_required = true;
    }

    // When > 0, updates are temporarily disabled
    int block_stacking_updates{0};

    // Propagate all clients after next update
    bool blocked_propagating_new_clients{false};

    bool restacking_required{false};
};

}
