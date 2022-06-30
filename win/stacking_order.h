/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <qobject.h>
#include <qobjectdefs.h>
#include <xcb/xcb.h>

#include <algorithm>
#include <deque>
#include <vector>

namespace KWin
{
class Toplevel;

namespace win
{

namespace x11
{
class window;
}

class space;

template<typename Order>
std::deque<Toplevel*> render_stack(Order& order)
{
    if (order.render_restack_required) {
        order.render_restack_required = false;
        order.render_overlays = {};
        Q_EMIT order.render_restack();
    }

    auto stack = order.stack;
    std::copy(std::begin(order.render_overlays),
              std::end(order.render_overlays),
              std::back_inserter(stack));
    return stack;
}

class KWIN_EXPORT stacking_order : public QObject
{
    Q_OBJECT
public:
    explicit stacking_order(win::space& space);

    void update(bool propagate_new_clients = false);

    void lock()
    {
        if (block_stacking_updates == 0)
            blocked_propagating_new_clients = false;
        ++block_stacking_updates;
    }

    void unlock()
    {
        if (--block_stacking_updates == 0) {
            update(blocked_propagating_new_clients);
            Q_EMIT unlocked();
        }
    }

    void force_restacking()
    {
        restacking_required = true;
        lock();
        unlock();
    }

    /// How windows are configured in z-direction. Topmost window at back.
    std::deque<Toplevel*> stack;
    std::deque<Toplevel*> pre_stack;

    /// Windows on top of the the stack that shall be composited addtionally.
    std::deque<Toplevel*> render_overlays;
    std::deque<xcb_window_t> manual_overlays;

    bool render_restack_required{false};

    win::space& space;

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
    void changed();

private:
    bool sort();

    // When > 0, updates are temporarily disabled
    int block_stacking_updates{0};

    // Propagate all clients after next update
    bool blocked_propagating_new_clients{false};

    bool restacking_required{false};
};

}
}
