/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "desktop_set.h"
#include "space_areas_helpers.h"
#include "toplevel.h"

#include "utils/blocker.h"

namespace KWin::win
{

template<typename Space>
void send_window_to_desktop(Space& space, Toplevel* window, int desk, bool dont_activate)
{
    if ((desk < 1 && desk != NET::OnAllDesktops)
        || desk > static_cast<int>(space.virtual_desktop_manager->count())) {
        return;
    }

    auto old_desktop = window->desktop();
    auto was_on_desktop = window->isOnDesktop(desk) || window->isOnAllDesktops();
    set_desktop(window, desk);

    if (window->desktop() != desk) {
        // No change or desktop forced
        return;
    }

    // window did range checking.
    desk = window->desktop();

    if (window->isOnDesktop(space.virtual_desktop_manager->current())) {
        if (win::wants_tab_focus(window) && kwinApp()->options->focusPolicyIsReasonable()
            && !was_on_desktop && // for stickyness changes
            !dont_activate) {
            request_focus(space, window);
        } else {
            restack_client_under_active(&space, window);
        }
    } else {
        raise_window(&space, window);
    }

    check_workspace_position(window, QRect(), old_desktop);

    auto const transients_stacking_order
        = restacked_by_space_stacking_order(&space, window->transient()->children);
    for (auto const& transient : transients_stacking_order) {
        if (transient->control) {
            send_window_to_desktop(space, transient, desk, dont_activate);
        }
    }

    update_space_areas(space);
}

template<typename Space>
void update_client_visibility_on_desktop_change(Space* space, uint newDesktop)
{
    for (auto const& toplevel : space->stacking_order->stack) {
        auto client = qobject_cast<x11::window*>(toplevel);
        if (!client || !client->control) {
            continue;
        }

        if (!client->isOnDesktop(newDesktop) && client != space->moveResizeClient()) {
            update_visibility(client);
        }
    }

    // Now propagate the change, after hiding, before showing.
    if (x11::rootInfo()) {
        x11::rootInfo()->setCurrentDesktop(space->virtual_desktop_manager->current());
    }

    if (auto move_resize_client = space->moveResizeClient()) {
        if (!move_resize_client->isOnDesktop(newDesktop)) {
            win::set_desktop(move_resize_client, newDesktop);
        }
    }

    auto const& list = space->stacking_order->stack;
    for (int i = list.size() - 1; i >= 0; --i) {
        auto client = qobject_cast<x11::window*>(list.at(i));
        if (!client || !client->control) {
            continue;
        }
        if (client->isOnDesktop(newDesktop)) {
            update_visibility(client);
        }
    }

    if (space->showingDesktop()) {
        // Do this only after desktop change to avoid flicker.
        space->setShowingDesktop(false);
    }
}

template<typename Space>
void handle_current_desktop_changed(Space& space, unsigned int oldDesktop, unsigned int newDesktop)
{
    close_active_popup(space);

    ++space.block_focus;
    blocker block(space.stacking_order);
    update_client_visibility_on_desktop_change(&space, newDesktop);

    // Restore the focus on this desktop
    --space.block_focus;

    activate_window_on_new_desktop(space, newDesktop);
    Q_EMIT space.qobject->currentDesktopChanged(oldDesktop, space.movingClient);
}

template<typename Space>
void handle_desktop_count_changed(Space& space, unsigned int /*prev*/, unsigned int next)
{
    reset_space_areas(space, next);
}

}
