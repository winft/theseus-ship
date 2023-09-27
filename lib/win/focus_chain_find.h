/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "desktop_get.h"
#include "screen.h"

#include <optional>

namespace KWin::win
{

/**
 * @brief Finds the best window to become the new active window in the focus chain for the given
 * virtual @p desktop on the given @p output.
 *
 * This method makes only sense to use if separate output focus is used. If separate output
 * focus is disabled the @p output is ignored. If no window for activation is found @c null is
 * returned.
 *
 * @param desktop The virtual desktop to look for a window for activation
 * @param output The output to constrain the search on with separate output focus
 * @return The window which could be activated or @c null if there is none.
 */
template<typename Space>
auto focus_chain_get_for_activation(Space& space, uint desktop, base::output const* output)
    -> std::optional<typename Space::window_t>
{
    auto& manager = space.stacking.focus_chain;

    auto desk_it = manager.chains.desktops.find(desktop);
    if (desk_it == manager.chains.desktops.end()) {
        return {};
    }

    auto const& chain = desk_it->second;

    // TODO(romangg): reverse-range with C++20
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (std::visit(overload{[&](auto&& win) {
                           if (!win->isShown()) {
                               return false;
                           }
                           if (manager.has_separate_screen_focus
                               && win->topo.central_output != output) {
                               return false;
                           }
                           return true;
                       }},
                       *it)) {
            return *it;
        }
    }

    return {};
}

template<typename Space>
auto focus_chain_get_for_activation_on_current_output(Space& space, uint desktop)
    -> std::optional<typename Space::window_t>
{
    return focus_chain_get_for_activation(space, desktop, get_current_output(space));
}

template<typename Space, typename Win, typename Output>
bool focus_chain_is_usable_focus_candidate(Space& space, Win& window, Output const* output)
{
    if (!window.isShown() || !on_current_desktop(window)) {
        return false;
    }

    if (!space.stacking.focus_chain.has_separate_screen_focus) {
        return true;
    }

    return on_screen(&window, output);
}

/**
 * @brief Queries the focus chain for @p output and @p desktop for the next window in relation to
 * the given @p reference.
 *
 * The method finds the first usable window which is not the @p reference Client. If no Client
 * can be found @c null is returned
 *
 * @param reference The reference window which should not be returned
 * @param desktop The virtual desktop whose focus chain should be used
 * @return *The next usable window or @c null if none can be found.
 */
template<typename Space, typename Output>
auto focus_chain_next(Space& space,
                      std::optional<typename Space::window_t> reference,
                      uint desktop,
                      Output const* output) -> std::optional<typename Space::window_t>
{
    auto& manager = space.stacking.focus_chain;

    auto desk_it = manager.chains.desktops.find(desktop);
    if (desk_it == manager.chains.desktops.end()) {
        return {};
    }

    auto const& chain = desk_it->second;

    // TODO(romangg): reverse-range with C++20
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (reference && *it == *reference) {
            continue;
        }
        if (std::visit(overload{[&](auto&& win) {
                           return focus_chain_is_usable_focus_candidate(space, *win, output);
                       }},
                       *it)) {
            return *it;
        }
    }

    return {};
}

}
