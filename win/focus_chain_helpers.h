/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "utils/algorithm.h"

namespace KWin::win
{

template<typename Manager, typename Win>
void focus_chain_remove(Manager& manager, Win* window)
{
    for (auto& [key, chain] : manager.chains.desktops) {
        remove_all(chain, window);
    }
    remove_all(manager.chains.latest_use, window);
}

/**
 * @brief Resizes the per virtual desktop focus chains from @p prev_size to @p next_size.
 *
 * This means that for each virtual desktop between previous and new size a new focus chain is
 * created and in case the number is reduced the focus chains are destroyed.
 *
 * @param prev_size The previous number of virtual desktops
 * @param next_size The new number of virtual desktops
 * @return void
 */
template<typename Manager>
void focus_chain_resize(Manager& manager, unsigned int prev_size, unsigned int next_size)
{
    for (auto i = prev_size + 1; i <= next_size; ++i) {
        manager.chains.desktops.insert({i, decltype(manager.chains.latest_use)()});
    }
    for (auto i = prev_size; i > next_size; --i) {
        manager.chains.desktops.erase(i);
    }
}

/**
 * Checks whether the focus chain for the given @p desktop contains the given @p window.
 * Does not consider the most recently used focus chain.
 */
template<typename Manager, typename Win>
bool focus_chain_at_desktop_contains(Manager& manager, Win* window, unsigned int desktop)
{
    auto it = manager.chains.desktops.find(desktop);
    if (it == manager.chains.desktops.end()) {
        return false;
    }
    return contains(it->second, window);
}

template<typename Win, typename ActWin, typename Chain>
void focus_chain_insert_window_into_chain(Win* window, Chain& chain, ActWin const* active_window)
{
    if (contains(chain, window)) {
        // TODO(romangg): better assert?
        return;
    }
    if (active_window && active_window != window && !chain.empty()
        && chain.back() == active_window) {
        // Add it after the active client
        chain.insert(std::prev(chain.end()), window);
    } else {
        // Otherwise add as the first one
        chain.push_back(window);
    }
}

template<typename Win, typename Chain>
void focus_chain_make_first_in_chain(Win* window, Chain& chain)
{
    remove_all(chain, window);
    chain.push_back(window);
}

template<typename Win, typename Chain>
void focus_chain_make_last_in_chain(Win* window, Chain& chain)
{
    remove_all(chain, window);
    chain.push_front(window);
}

template<typename Win, typename ActWin, typename Chain>
void focus_chain_update_window_in_chain(Win* window,
                                        focus_chain_change change,
                                        Chain& chain,
                                        ActWin const* active_window)
{
    if (change == focus_chain_change::make_first) {
        focus_chain_make_first_in_chain(window, chain);
    } else if (change == focus_chain_change::make_last) {
        focus_chain_make_last_in_chain(window, chain);
    } else {
        focus_chain_insert_window_into_chain(window, chain, active_window);
    }
}

/**
 * @brief Returns the first window in the most recently used focus chain. First window in this
 * case means really the first window in the chain and not the most recently used window.
 *
 * @return The first window in the most recently used chain.
 */
template<typename Win, typename Manager>
Win* focus_chain_first_latest_use(Manager& manager)
{
    auto& latest_chain = manager.chains.latest_use;
    if (latest_chain.empty()) {
        return nullptr;
    }

    return latest_chain.front();
}

/**
 * @brief Queries the most recently used focus chain for the next window after the given
 * @p reference.
 *
 * The navigation wraps around the borders of the chain. That is if the @p reference window is
 * the last item of the focus chain, the first window will be returned.
 *
 * If the @p reference window cannot be found in the focus chain, the first element of the focus
 * chain is returned.
 *
 * @param reference The start point in the focus chain to search
 * @return The relatively next window in the most recently used chain.
 */
template<typename Manager, typename Win>
Win* focus_chain_next_latest_use(Manager& manager, Win* reference)
{
    auto& latest_chain = manager.chains.latest_use;
    if (latest_chain.empty()) {
        return nullptr;
    }

    auto it = find(latest_chain, reference);

    if (it == latest_chain.end()) {
        return latest_chain.front();
    }
    if (it == latest_chain.begin()) {
        return latest_chain.back();
    }

    return *std::prev(it);
}

}
