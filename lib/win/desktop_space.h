/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "desktop_set.h"
#include "focus_blocker.h"
#include "space_areas_helpers.h"

#include "utils/blocker.h"

namespace KWin::win
{

template<typename Space, typename Win>
void send_window_to_desktop(Space& space, Win* window, int desk, bool dont_activate)
{
    if ((desk < 1 && desk != x11::net::win_info::OnAllDesktops)
        || desk > static_cast<int>(space.virtual_desktop_manager->count())) {
        return;
    }

    auto old_desktop = get_desktop(*window);
    auto was_on_desktop = on_desktop(window, desk) || on_all_desktops(window);
    set_desktop(window, desk);

    if (get_desktop(*window) != desk) {
        // No change or desktop forced
        return;
    }

    // window did range checking.
    desk = get_desktop(*window);

    if (on_desktop(window, space.virtual_desktop_manager->current())) {
        if (win::wants_tab_focus(window) && space.base.options->qobject->focusPolicyIsReasonable()
            && !was_on_desktop && // for stickyness changes
            !dont_activate) {
            request_focus(space, *window);
        } else {
            restack_client_under_active(space, *window);
        }
    } else {
        raise_window(space, window);
    }

    check_workspace_position(window, QRect(), old_desktop);

    auto const transients_stacking_order
        = restacked_by_space_stacking_order(space, window->transient->children);
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

    if (auto& mov_res = space->move_resize_window) {
        std::visit(overload{[&](auto&& win) {
                       if (!on_desktop(win, newDesktop)) {
                           win::set_desktop(win, newDesktop);
                       }
                   }},
                   *mov_res);
    }

    space->handle_desktop_changed(newDesktop);
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

    // TODO: why is win.space.base.options->isRollOverDesktops() not honored?
    auto const desktop = functor(nullptr, true);

    if (!is_desktop(&window) && !is_dock(&window)) {
        set_move_resize_window(ws, window);
        vds->setCurrent(desktop);
        unset_move_resize_window(ws);
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
    auto&& base = space.base;
    auto const& outputs = base.outputs;

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

    if (auto& effects = space.base.render->compositor->effects) {
        effects->desktopResized(size);
    }
}

}
