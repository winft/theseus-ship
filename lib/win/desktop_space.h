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
void send_window_to_subspace(Space& space, Win* window, int desk, bool dont_activate)
{
    if ((desk < 1 && desk != x11_desktop_number_on_all)
        || desk > static_cast<int>(space.subspace_manager->subspaces.size())) {
        return;
    }

    auto old_subspace = get_subspace(*window);
    auto was_on_subspace = on_subspace(*window, desk) || on_all_subspaces(*window);
    set_subspace(*window, desk);

    if (get_subspace(*window) != desk) {
        // No change or subspace forced
        return;
    }

    // window did range checking.
    desk = get_subspace(*window);

    if (on_subspace(*window, space.subspace_manager->current_x11id())) {
        if (win::wants_tab_focus(window) && space.options->qobject->focusPolicyIsReasonable()
            && !was_on_subspace && // for stickyness changes
            !dont_activate) {
            request_focus(space, *window);
        } else {
            restack_client_under_active(space, *window);
        }
    } else {
        raise_window(space, window);
    }

    check_workspace_position(window, QRect(), old_subspace);

    auto const transients_stacking_order
        = restacked_by_space_stacking_order(space, window->transient->children);
    for (auto const& transient : transients_stacking_order) {
        if (transient->control) {
            send_window_to_subspace(space, transient, desk, dont_activate);
        }
    }

    update_space_areas(space);
}

template<typename Space>
void update_client_visibility_on_subspace_change(Space* space, uint subspace)
{
    // Restore the focus on this subspace afterwards.
    focus_blocker<Space> blocker(*space);

    if (auto& mov_res = space->move_resize_window) {
        std::visit(overload{[&](auto&& win) {
                       if (!on_subspace(*win, subspace)) {
                           win::set_subspace(*win, subspace);
                       }
                   }},
                   *mov_res);
    }

    space->handle_subspace_changed(subspace);
}

template<typename Space>
void handle_subspace_count_changed(Space& space, unsigned int /*prev*/, unsigned int next)
{
    reset_space_areas(space, next);
}

template<typename Win>
void window_to_subspace(Win& window, subspace& sub)
{
    auto& ws = window.space;
    auto& vds = ws.subspace_manager;

    if (!is_desktop(&window) && !is_dock(&window)) {
        set_move_resize_window(ws, window);
        vds->setCurrent(sub);
        unset_move_resize_window(ws);
    }
}

template<typename Win>
void window_to_next_subspace(Win& window)
{
    // TODO: why is get_nav_wraps not honored?
    window_to_subspace(window,
                       window.space.subspace_manager->get_successor_of(
                           *window.space.subspace_manager->current, true));
}

template<typename Win>
void window_to_prev_subspace(Win& window)
{
    // TODO: why is get_nav_wraps not honored?
    window_to_subspace(window,
                       window.space.subspace_manager->get_predecessor_of(
                           *window.space.subspace_manager->current, true));
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

    if (auto& effects = space.base.render->effects) {
        effects->desktopResized(size);
    }
}

}
