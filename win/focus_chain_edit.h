/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net.h"
#include "types.h"
#include "util.h"

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
 * @brief Updates the position of the @p window according to the requested @p change in the
 * focus chain.
 *
 * This method affects both the most recently used focus chain and the per virtual desktop focus
 * chain.
 *
 * In case the client does no longer want to get focus, it is removed from all chains. In case
 * the client is on all virtual desktops it is ensured that it is present in each of the virtual
 * desktops focus chain. In case it's on exactly one virtual desktop it is ensured that it is
 * only in the focus chain for that virtual desktop.
 *
 * Depending on @p change the window is inserted at different positions in the focus chain. In
 * case of @c focus_chain_change::make_first it is moved to the first position of the chain, in case
 * of @c focus_chain_change::make_last it is moved to the last position of the chain. In all other
 * cases it depends on whether the @p window is the currently active window. If it is the active
 * window it becomes the first Client in the chain, otherwise it is inserted at the second position
 * that is directly after the currently active window.
 *
 * @param window The window which should be moved inside the chains.
 * @param change Where to move the window
 */
template<typename Manager, typename Win>
void focus_chain_update(Manager& manager, Win* window, focus_chain_change change)
{
    if (!wants_tab_focus(window)) {
        // Doesn't want tab focus, remove.
        focus_chain_remove(manager, window);
        return;
    }

    if (window->isOnAllDesktops()) {
        // Now on all desktops, add it to focus chains it is not already in.
        for (auto& [key, chain] : manager.chains.desktops) {
            // Making first/last works only on current desktop, don't affect all desktops
            if (key == manager.current_desktop
                && (change == focus_chain_change::make_first
                    || change == focus_chain_change::make_last)) {
                if (change == focus_chain_change::make_first) {
                    focus_chain_make_first_in_chain(window, chain);
                } else {
                    focus_chain_make_last_in_chain(window, chain);
                }
            } else {
                focus_chain_insert_window_into_chain(window, chain, manager.active_window);
            }
        }
    } else {
        // Now only on desktop, remove it anywhere else
        for (auto& [key, chain] : manager.chains.desktops) {
            if (window->isOnDesktop(key)) {
                focus_chain_update_window_in_chain(window, change, chain, manager.active_window);
            } else {
                remove_all(chain, window);
            }
        }
    }

    // add for most recently used chain
    focus_chain_update_window_in_chain(
        window, change, manager.chains.latest_use, manager.active_window);
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

template<typename Chain, typename Win>
void focus_chain_move_window_after_in_chain(Chain& chain, Win* window, Win* reference)
{
    if (!contains(chain, reference)) {
        // TODO(romangg): better assert?
        return;
    }

    remove_all(chain, window);

    if (belong_to_same_client(reference, window)) {
        // Simple case, just put it directly behind the reference window of the same client.
        // TODO(romangg): can this special case be explained better?
        auto it = find(chain, reference);
        chain.insert(it, window);
        return;
    }

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (belong_to_same_client(reference, *it)) {
            chain.insert(std::next(it).base(), window);
            return;
        }
    }
}

/**
 * @brief Moves @p window behind the @p reference in all focus chains.
 *
 * @param client The Client to move in the chains
 * @param reference The Client behind which the @p client should be moved
 * @return void
 */
template<typename Win, typename Manager>
void focus_chain_move_window_after(Manager& manager, Win* window, Win* reference)
{
    if (!wants_tab_focus(window)) {
        return;
    }

    for (auto& [key, chain] : manager.chains.desktops) {
        if (!window->isOnDesktop(key)) {
            continue;
        }
        focus_chain_move_window_after_in_chain(chain, window, reference);
    }

    focus_chain_move_window_after_in_chain(manager.chains.latest_use, window, reference);
}

}
