/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <utils/algorithm.h>
#include <utils/geo.h>
#include <win/layers.h>
#include <win/placement.h>
#include <win/setup.h>
#include <win/space_areas_helpers.h>
#include <win/tabbox.h>
#include <win/wayland/idle.h>
#include <win/wayland/space_areas.h>
#include <win/wayland/transient.h>

#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Space>
auto space_windows_find(Space const& space, Wrapland::Server::Surface const* surface) ->
    typename Space::wayland_window*
{
    using window = typename Space::wayland_window;

    if (!surface) {
        // TODO(romangg): assert instead?
        return nullptr;
    }

    auto it = std::find_if(space.windows.cbegin(), space.windows.cend(), [surface](auto win) {
        return std::visit(overload{[&](window* win) { return win->surface == surface; },
                                   [&](auto&& /*win*/) { return false; }},
                          win);
    });
    return it != space.windows.cend() ? std::get<window*>(*it) : nullptr;
}

template<typename Space>
auto space_windows_find_internal(Space const& space, QWindow const* window) ->
    typename Space::internal_window_t*
{
    using win_t = typename Space::internal_window_t;

    if (!window) {
        return nullptr;
    }

    for (auto win : space.windows) {
        if (!std::holds_alternative<win_t*>(win)) {
            continue;
        }

        auto internal = std::get<win_t*>(win);
        if (internal->internalWindow() == window) {
            return internal;
        }
    }

    return nullptr;
}

template<typename Space>
void space_windows_add(Space& space, typename Space::wayland_window& window)
{
    using wayland_window = typename Space::wayland_window;

    if (window.control && !window.layer_surface) {
        setup_space_window_connections(&space, &window);
        window.updateDecoration(false);
        update_layer(&window);

        auto const area = space_window_area(
            space, area_option::placement, get_current_output(window.space), get_subspace(window));
        auto placementDone = false;

        if (window.isInitialPositionSet()) {
            placementDone = true;
        }
        if (window.control->fullscreen) {
            placementDone = true;
        }
        if (window.maximizeMode() == maximize_mode::full) {
            placementDone = true;
        }
        if (window.control->rules.checkPosition(geo::invalid_point, true) != geo::invalid_point) {
            placementDone = true;
        }
        if (!placementDone) {
            place_in_area(&window, area);
        }
    }

    assert(!contains(space.stacking.order.pre_stack, typename Space::window_t(&window)));
    space.stacking.order.pre_stack.push_back(&window);
    space.stacking.order.update_order();

    if (window.control) {
        win::update_space_areas(space);

        if (window.wantsInput() && !window.control->minimized) {
            activate_window(space, window);
        }

        update_tabbox(space);

        QObject::connect(window.qobject.get(),
                         &wayland_window::qobject_t::windowShown,
                         space.qobject.get(),
                         [&space, &window] {
                             update_layer(&window);
                             space.stacking.order.update_count();
                             win::update_space_areas(space);
                             if (window.wantsInput()) {
                                 activate_window(space, window);
                             }
                         });
        QObject::connect(window.qobject.get(),
                         &wayland_window::qobject_t::windowHidden,
                         space.qobject.get(),
                         [&space] {
                             // TODO: update tabbox if it's displayed
                             space.stacking.order.update_count();
                             win::update_space_areas(space);
                         });

        idle_setup(window);
    }

    adopt_transient_children(&space, &window);
    Q_EMIT space.qobject->wayland_window_added(window.meta.signal_id);
}

template<typename Space>
void space_windows_remove(Space& space, typename Space::wayland_window& window)
{
    using window_t = typename Space::window_t;

    remove_all(space.windows, window_t(&window));

    if (window.control) {
        if (window_t(&window) == space.stacking.most_recently_raised) {
            space.stacking.most_recently_raised = {};
        }
        if (window_t(&window) == space.stacking.delayfocus_window) {
            cancel_delay_focus(space);
        }
        if (window_t(&window) == space.stacking.last_active) {
            space.stacking.last_active = {};
        }
        if (window_t(&window) == space.client_keys_client) {
            shortcut_dialog_done(space, false);
        }
        if (!window.control->shortcut.isEmpty()) {
            // Remove from client_keys.
            set_shortcut(&window, QString{});
        }
        process_window_hidden(space, window);
        Q_EMIT space.qobject->clientRemoved(window.meta.signal_id);
    }

    space.stacking.order.update_count();

    if (window.control) {
        win::update_space_areas(space);
        update_tabbox(space);
    }

    Q_EMIT space.qobject->wayland_window_removed(window.meta.signal_id);
}

}
