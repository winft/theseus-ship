/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "focus_chain.h"
#include "move.h"
#include "net.h"
#include "stacking.h"
#include "transient.h"
#include "types.h"
#include "virtual_desktops.h"

#include "base/output_helpers.h"
#include "main.h"

#include <Wrapland/Server/plasma_window.h>

namespace KWin::win
{

template<typename Win>
bool on_screen(Win* win, base::output const* output)
{
    if (!output) {
        return false;
    }
    return output->geometry().intersects(win->frameGeometry());
}

/**
 * @brief Finds the best window to become the new active window in the focus chain for the given
 * virtual @p desktop.
 *
 * In case that separate output focus is used only windows on the current output are considered.
 * If no window for activation is found @c null is returned.
 *
 * @param desktop The virtual desktop to look for a window for activation
 * @return The window which could be activated or @c null if there is none.
 */
template<typename Space>
base::output const* get_current_output(Space const& space)
{
    auto const& base = kwinApp()->get_base();

    if (kwinApp()->options->get_current_output_follows_mouse()) {
        return base::get_nearest_output(base.get_outputs(), input::get_cursor()->pos());
    }

    auto const cur = base.topology.current;
    if (auto client = space.active_client; client && !win::on_screen(client, cur)) {
        return client->central_output;
    }
    return cur;
}

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
template<typename Win, typename Manager>
Win* focus_chain_get_for_activation(Manager& manager, uint desktop, base::output const* output)
{
    auto desk_it = manager.chains.desktops.find(desktop);
    if (desk_it == manager.chains.desktops.end()) {
        return nullptr;
    }

    auto const& chain = desk_it->second;

    // TODO(romangg): reverse-range with C++20
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        // TODO: move the check into Client
        auto win = *it;
        if (!win->isShown()) {
            continue;
        }
        if (manager.has_separate_screen_focus && win->central_output != output) {
            continue;
        }
        return win;
    }

    return nullptr;
}

template<typename Win, typename Manager>
Win* focus_chain_get_for_activation_on_current_output(Manager& manager, uint desktop)
{
    return focus_chain_get_for_activation<Win>(manager, desktop, get_current_output(manager.space));
}

template<typename Manager>
bool focus_chain_is_usable_focus_candidate(Manager& manager, Toplevel* window, Toplevel* prev)
{
    if (window == prev) {
        return false;
    }
    if (!window->isShown() || !window->isOnCurrentDesktop()) {
        return false;
    }

    if (!manager.has_separate_screen_focus) {
        return true;
    }

    return on_screen(window, prev ? prev->central_output : get_current_output(manager.space));
}

/**
 * @brief Queries the focus chain for @p desktop for the next window in relation to the given
 * @p reference.
 *
 * The method finds the first usable window which is not the @p reference Client. If no Client
 * can be found @c null is returned
 *
 * @param reference The reference window which should not be returned
 * @param desktop The virtual desktop whose focus chain should be used
 * @return *The next usable window or @c null if none can be found.
 */
template<typename Manager, typename Win>
Toplevel* focus_chain_next_for_desktop(Manager& manager, Win* reference, uint desktop)
{
    auto desk_it = manager.chains.desktops.find(desktop);
    if (desk_it == manager.chains.desktops.end()) {
        return nullptr;
    }

    auto const& chain = desk_it->second;

    // TODO(romangg): reverse-range with C++20
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (focus_chain_is_usable_focus_candidate(manager, *it, reference)) {
            return *it;
        }
    }

    return nullptr;
}

template<typename Base, typename Win>
void set_current_output_by_window(Base& base, Win const& window)
{
    if (!window.control->active()) {
        return;
    }
    if (window.central_output && !win::on_screen(&window, base.topology.current)) {
        base::set_current_output(base, window.central_output);
    }
}

template<typename Win>
bool on_active_screen(Win* win)
{
    return on_screen(win, get_current_output(win->space));
}

}
