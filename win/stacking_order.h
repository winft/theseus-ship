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

class KWIN_EXPORT stacking_order : public QObject
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
    /**
     * This signal is emitted when the stacking order changed, i.e. a window is risen
     * or lowered
     */
    void changed();

private:
    bool sort();
    void propagate_clients(bool propagate_new_clients);

    std::deque<xcb_window_t> manual_overlays;

    // When > 0, updates are temporarily disabled
    int block_stacking_updates{0};

    // Propagate all clients after next update
    bool blocked_propagating_new_clients{false};

    bool restacking_required{false};

public:
    // How windows are configured in z-direction.
    std::deque<Toplevel*> win_stack;
    // Unsorted deque
    std::deque<Toplevel*> pre_stack;

    /**
     * Returns the list of clients sorted in stacking order, with topmost client
     * at the last position
     */
    std::deque<Toplevel*> const& sorted() const
    {
        // TODO: Q_ASSERT( block_stacking_updates == 0 );
        return win_stack;
    }

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

    void add_manual_overlay(xcb_window_t id)
    {
        manual_overlays.push_back(id);
    }

    void remove_manual_overlay(xcb_window_t id)
    {
        manual_overlays.erase(std::find(manual_overlays.begin(), manual_overlays.end(), id));
    }
};

}
}
