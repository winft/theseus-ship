/*
    SPDX-FileCopyrightText: ...

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "controlling.h"
#include "focuschain.h"
#include "meta.h"
#include "stacking_order.h"
#include "transient.h"
#include "util.h"

#include "options.h"
#include "utils.h"
#include "virtualdesktops.h"
#include "workspace.h"

#include "win/x11/group.h"
#include "win/x11/window.h"

namespace KWin
{
class Toplevel;

namespace win
{

/**
 * Returns topmost visible client. Windows on the dock, the desktop
 * or of any other special kind are excluded. Also if the window
 * doesn't accept focus it's excluded.
 */
// TODO misleading name for this method, too many slightly different ways to use it
template<typename Space>
Toplevel* top_client_on_desktop(Space* space,
                                int desktop,
                                int screen,
                                bool unconstrained = false,
                                bool only_normal = true)
{
    // TODO    Q_ASSERT( block_stacking_updates == 0 );
    const auto& list
        = unconstrained ? space->stacking_order->pre_stack : space->stacking_order->sorted();
    for (auto it = std::crbegin(list); it != std::crend(list); it++) {
        auto c = *it;
        if (c && c->isOnDesktop(desktop) && c->isShown() && c->isOnCurrentActivity()) {
            if (screen != -1 && c->screen() != screen)
                continue;
            if (!only_normal)
                return c;
            if (wants_tab_focus(c) && !is_special_window(c))
                return c;
        }
    }
    return nullptr;
}

template<typename Space>
Toplevel* find_desktop(Space* space, bool topmost, int desktop)
{
    // TODO    Q_ASSERT( block_stacking_updates == 0 );
    // TODO(fsorr): use C++20 std::ranges::reverse_view
    const auto& list = space->stacking_order->sorted();
    if (topmost) {
        for (auto it = std::crbegin(list); it != std::crend(list); it++) {
            auto window = *it;
            if (window->control && window->isOnDesktop(desktop) && is_desktop(window)
                && window->isShown()) {
                return window;
            }
        }
    } else { // bottom-most
        for (auto const& window : list) {
            if (window->control && window->isOnDesktop(desktop) && is_desktop(window)
                && window->isShown()) {
                return window;
            }
        }
    }
    return nullptr;
}

template<typename Space, typename Window>
void lower_window(Space* space, Window* window)
{
    assert(window->control);

    auto do_lower = [space](auto win) {
        win->control->cancel_auto_raise();

        Blocker blocker(space->stacking_order);

        remove_all(space->stacking_order->pre_stack, win);
        space->stacking_order->pre_stack.push_front(win);

        return blocker;
    };
    auto cleanup = [space](auto win) {
        if (win == space->most_recently_raised) {
            space->most_recently_raised = nullptr;
        }
    };

    auto blocker = do_lower(window);

    if (window->isTransient() && window->group()) {
        // Lower also all windows in the group, in reversed stacking order.
        auto const wins = space->ensureStackingOrder(window->group()->members());

        for (auto it = wins.crbegin(); it != wins.crend(); it++) {
            auto gwin = *it;
            if (gwin == static_cast<Toplevel*>(window)) {
                continue;
            }

            assert(gwin->control);
            do_lower(gwin);
            cleanup(gwin);
        }
    }

    cleanup(window);
}

template<typename Space, typename Window>
void raise_window(Space* space, Window* window)
{
    if (!window) {
        return;
    }

    auto prepare = [space](auto window) {
        assert(window->control);
        window->control->cancel_auto_raise();
        return Blocker(space->stacking_order);
    };
    auto do_raise = [space](auto window) {
        remove_all(space->stacking_order->pre_stack, window);
        space->stacking_order->pre_stack.push_back(window);

        if (!is_special_window(window)) {
            space->most_recently_raised = static_cast<Toplevel*>(window);
        }
    };

    auto blocker = prepare(window);

    if (window->isTransient()) {
        // Also raise all leads.
        std::vector<Toplevel*> leads;

        for (auto lead : window->transient()->leads()) {
            while (lead) {
                if (!contains(leads, lead)) {
                    leads.push_back(lead);
                }
                lead = lead->transient()->lead();
            }
        }

        auto stacked_leads = space->ensureStackingOrder(leads);

        for (auto lead : stacked_leads) {
            if (!lead->control) {
                // Might be without control, at least on X11 this can happen (latte-dock settings).
                continue;
            }
            auto blocker = prepare(lead);
            do_raise(lead);
        }
    }

    do_raise(window);
}

template<typename Space, typename Window>
void raise_or_lower_client(Space* space, Window* window)
{
    if (!window) {
        return;
    }

    Toplevel* topmost = nullptr;

    if (space->most_recently_raised
        && contains(space->stacking_order->sorted(), space->most_recently_raised)
        && space->most_recently_raised->isShown() && window->isOnCurrentDesktop()) {
        topmost = space->most_recently_raised;
    } else {
        topmost = top_client_on_desktop(space,
                                        window->isOnAllDesktops()
                                            ? VirtualDesktopManager::self()->current()
                                            : window->desktop(),
                                        options->isSeparateScreenFocus() ? window->screen() : -1);
    }

    if (window == topmost) {
        lower_window(space, window);
    } else {
        raise_window(space, window);
    }
}

template<typename Space, typename Window>
void restack(Space* space, Window* window, Toplevel* under, bool force = false)
{
    assert(contains(space->stacking_order->pre_stack, under));

    if (!force && !belong_to_same_client(under, window)) {
        // put in the stacking order below _all_ windows belonging to the active application
        for (auto it = space->stacking_order->pre_stack.crbegin();
             it != space->stacking_order->pre_stack.crend();
             it++) {
            auto other = *it;
            if (other->control && other->layer() == window->layer()
                && belong_to_same_client(under, other)) {
                under = (window == other) ? nullptr : other;
                break;
            }
        }
    }
    if (under) {
        remove_all(space->stacking_order->pre_stack, window);
        auto it = find(space->stacking_order->pre_stack, under);
        space->stacking_order->pre_stack.insert(it, window);
    }

    assert(contains(space->stacking_order->pre_stack, window));
    FocusChain::self()->moveAfterClient(window, under);
    space->stacking_order->update();
}

template<typename Space, typename Win>
void restack_client_under_active(Space* space, Win* window)
{
    if (!space->active_client || space->active_client == window
        || space->active_client->layer() != window->layer()) {
        raise_window(space, window);
        return;
    }
    restack(space, window, space->active_client);
}

}
}
