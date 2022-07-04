/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "space_areas_helpers.h"
#include "space_helpers.h"
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
Toplevel* find_window_to_activate_on_desktop(Space& space, unsigned int desktop)
{
    if (space.movingClient != nullptr && space.active_client == space.movingClient
        && focus_chain_at_desktop_contains(space.focus_chain, space.active_client, desktop)
        && space.active_client->isShown() && space.active_client->isOnCurrentDesktop()) {
        // A requestFocus call will fail, as the client is already active
        return space.active_client;
    }

    // from actiavtion.cpp
    if (kwinApp()->options->isNextFocusPrefersMouse()) {
        auto it = space.stacking_order->stack.cend();
        while (it != space.stacking_order->stack.cbegin()) {
            auto client = qobject_cast<win::x11::window*>(*(--it));
            if (!client) {
                continue;
            }

            if (!(client->isShown() && client->isOnDesktop(desktop) && on_active_screen(client)))
                continue;

            if (client->frameGeometry().contains(input::get_cursor()->pos())) {
                if (!is_desktop(client)) {
                    return client;
                }
                // Unconditional break, we don't pass focus to some client below an unusable one.
                break;
            }
        }
    }

    return focus_chain_get_for_activation_on_current_output<Toplevel>(space.focus_chain, desktop);
}

template<typename Space>
void activate_window_on_new_desktop(Space& space, unsigned int desktop)
{
    Toplevel* c = nullptr;

    if (kwinApp()->options->focusPolicyIsReasonable()) {
        c = find_window_to_activate_on_desktop(space, desktop);
    }

    // If "unreasonable focus policy" and active_client is on_all_desktops and
    // under mouse (Hence == old_active_client), conserve focus.
    // (Thanks to Volker Schatz <V.Schatz at thphys.uni-heidelberg.de>)
    else if (space.active_client && space.active_client->isShown()
             && space.active_client->isOnCurrentDesktop()) {
        c = space.active_client;
    }

    if (!c) {
        c = find_desktop(&space, true, desktop);
    }

    if (c != space.active_client) {
        set_active_window(space, nullptr);
    }

    if (c) {
        request_focus(space, c);
    } else if (auto desktop_client = find_desktop(&space, true, desktop)) {
        request_focus(space, desktop_client);
    } else {
        focus_to_null(space);
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
