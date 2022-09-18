/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "hide.h"
#include "transient.h"

#include "win/space_areas_helpers.h"
#include "win/tabbox.h"

namespace KWin::win::x11
{

template<typename Space, typename Win>
void add_controlled_window_to_space(Space& space, Win* win)
{
    auto grp = find_group(space, win->xcb_window);

    space.windows.push_back(win);
    Q_EMIT space.qobject->clientAdded(win->signal_id);

    if (grp) {
        grp->gotLeader(win);
    }

    if (is_desktop(win)) {
        if (!space.stacking.active && space.stacking.should_get_focus.empty()
            && on_current_desktop(win)) {
            // TODO: Make sure desktop is active after startup if there's no other window active
            request_focus(space, win);
        }
    } else {
        focus_chain_update(space.stacking.focus_chain, win, focus_chain_change::update);
    }

    if (!contains(space.stacking.order.pre_stack, win)) {
        // Raise if it hasn't got any stacking position yet
        space.stacking.order.pre_stack.push_back(win);
    }
    if (!contains(space.stacking.order.stack, win)) {
        // It'll be updated later, and updateToolWindows() requires c to be in stacking.order.
        space.stacking.order.stack.push_back(win);
    }

    // This cannot be in manage(), because the client got added only now
    update_space_areas(space);
    update_layer(win);

    if (is_desktop(win)) {
        raise_window(&space, win);
        // If there's no active client, make this desktop the active one
        if (!space.stacking.active && space.stacking.should_get_focus.empty())
            activate_window(space,
                            find_desktop(&space, true, space.virtual_desktop_manager->current()));
    }

    check_active_modal<Win>(space);

    for (auto window : space.windows) {
        window->checkTransient(win);
    }

    // Propagate new client
    space.stacking.order.update_count();

    if (is_utility(win) || is_menu(win) || is_toolbar(win)) {
        update_tool_windows_visibility(&space, true);
    }

    update_tabbox(space);
}

}
