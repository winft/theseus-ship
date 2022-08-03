/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "desktop_set.h"
#include "focus_blocker.h"
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
        if (win::wants_tab_focus(window) && kwinApp()->options->qobject->focusPolicyIsReasonable()
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
    // Restore the focus on this desktop afterwards.
    focus_blocker<Space> blocker(*space);

    if (auto move_resize_client = space->move_resize_window) {
        if (!move_resize_client->isOnDesktop(newDesktop)) {
            win::set_desktop(move_resize_client, newDesktop);
        }
    }

    for (auto const& toplevel : space->stacking_order->stack) {
        auto client = qobject_cast<x11::window*>(toplevel);
        if (!client || !client->control) {
            continue;
        }

        if (!client->isOnDesktop(newDesktop) && client != space->move_resize_window) {
            update_visibility(client);
        }
    }

    // Now propagate the change, after hiding, before showing.
    if (x11::rootInfo()) {
        x11::rootInfo()->setCurrentDesktop(space->virtual_desktop_manager->current());
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

    if (space->showing_desktop) {
        // Do this only after desktop change to avoid flicker.
        set_showing_desktop(*space, false);
    }
}

template<typename Space>
void handle_desktop_count_changed(Space& space, unsigned int /*prev*/, unsigned int next)
{
    reset_space_areas(space, next);
}

template<typename Direction, typename Win>
void window_to_desktop(Win& window)
{
    auto& ws = window.space;
    auto& vds = ws.virtual_desktop_manager;
    Direction functor(*vds);

    // TODO: why is kwinApp()->options->isRollOverDesktops() not honored?
    auto const desktop = functor(nullptr, true);

    if (!is_desktop(&window) && !is_dock(&window)) {
        set_move_resize_window(ws, &window);
        vds->setCurrent(desktop);
        set_move_resize_window(ws, nullptr);
    }
}

template<typename Win>
void window_to_next_desktop(Win& window)
{
    window_to_desktop<win::virtual_desktop_next>(window);
}

template<typename Win>
void window_to_prev_desktop(Win& window)
{
    window_to_desktop<win::virtual_desktop_previous>(window);
}

template<typename Space>
void save_old_output_sizes(Space& space)
{
    auto&& base = kwinApp()->get_base();
    auto const& outputs = base.get_outputs();

    space.olddisplaysize = base.topology.size;
    space.oldscreensizes.clear();

    for (auto output : outputs) {
        space.oldscreensizes.push_back(output->geometry());
    }
}

/// After an output topology change.
template<typename Space>
void handle_desktop_resize(Space& space, QSize const& size)
{
    update_space_areas(space);

    // after updateClientArea(), so that one still uses the previous one
    save_old_output_sizes(space);

    // TODO: emit a signal instead and remove the deep function calls into edges and effects
    space.edges->recreateEdges();

    if (auto& effects = space.render.effects) {
        effects->desktopResized(size);
    }
}

}
