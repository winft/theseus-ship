/*
    SPDX-FileCopyrightText: ...

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "options.h"
#include "virtualdesktops.h"
#include "workspace.h"

#include "win/x11/group.h"
#include "win/x11/window.h"

namespace KWin
{
class Toplevel;

namespace win
{

template<typename Space, typename Window>
void lower_window(Space* space, Window* window)
{
    assert(window->control);

    auto do_lower = [space](auto win) {
        win->control->cancel_auto_raise();

        StackingUpdatesBlocker blocker(space);

        remove_all(space->unconstrained_stacking_order, win);
        space->unconstrained_stacking_order.push_front(win);

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
void raise_or_lower_client(Space* space, Window* window)
{
    if (!window) {
        return;
    }

    Toplevel* topmost = nullptr;

    if (space->most_recently_raised && contains(space->stacking_order, space->most_recently_raised)
        && space->most_recently_raised->isShown() && window->isOnCurrentDesktop()) {
        topmost = space->most_recently_raised;
    } else {
        topmost = space->topClientOnDesktop(
            window->isOnAllDesktops() ? VirtualDesktopManager::self()->current()
                                      : window->desktop(),
            options->isSeparateScreenFocus() ? window->screen() : -1);
    }

    if (window == topmost) {
        lower_window(space, window);
    } else {
        space->raise_window(window);
    }
}

}
}
