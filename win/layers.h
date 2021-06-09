/*
    SPDX-FileCopyrightText: ...

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "options.h"
#include "virtualdesktops.h"

namespace KWin
{
class Toplevel;

namespace win
{

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
        space->lower_window(window);
    } else {
        space->raise_window(window);
    }
}

}
}
