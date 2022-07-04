/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "focus_chain.h"
#include "focus_chain_helpers.h"
#include "net.h"

#include "rules/rules.h"

namespace KWin::win
{

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

template<typename Win>
void set_skip_pager(Win* win, bool set)
{
    set = win->control->rules().checkSkipPager(set);
    if (set == win->control->skip_pager()) {
        return;
    }

    win->control->set_skip_pager(set);
    win->updateWindowRules(Rules::SkipPager);
    Q_EMIT win->skipPagerChanged();
}

template<typename Win>
void set_skip_switcher(Win* win, bool set)
{
    set = win->control->rules().checkSkipSwitcher(set);
    if (set == win->control->skip_switcher()) {
        return;
    }

    win->control->set_skip_switcher(set);
    win->updateWindowRules(Rules::SkipSwitcher);
    Q_EMIT win->skipSwitcherChanged();
}

template<typename Win>
void set_skip_taskbar(Win* win, bool set)
{
    if (set == win->control->skip_taskbar()) {
        return;
    }

    auto const was_wants_tab_focus = win::wants_tab_focus(win);

    win->control->set_skip_taskbar(set);
    win->updateWindowRules(Rules::SkipTaskbar);

    if (was_wants_tab_focus != win::wants_tab_focus(win)) {
        focus_chain_update(win->space.focus_chain,
                           win,
                           win->control->active() ? focus_chain_change::make_first
                                                  : focus_chain_change::update);
    }

    Q_EMIT win->skipTaskbarChanged();
}

template<typename Win>
void set_original_skip_taskbar(Win* win, bool set)
{
    auto const rules_checked = win->control->rules().checkSkipTaskbar(set);
    win->control->set_original_skip_taskbar(rules_checked);
    win::set_skip_taskbar(win, rules_checked);
}

}
