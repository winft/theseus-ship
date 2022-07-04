/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <NETWM>

namespace KWin::win::x11
{

template<typename Win>
void update_allowed_actions(Win* win, bool force = false)
{
    if (!win->control && !force) {
        return;
    }

    auto old_allowed_actions = NET::Actions(win->allowed_actions);
    win->allowed_actions = NET::Actions();

    if (win->isMovable()) {
        win->allowed_actions |= NET::ActionMove;
    }
    if (win->isResizable()) {
        win->allowed_actions |= NET::ActionResize;
    }
    if (win->isMinimizable()) {
        win->allowed_actions |= NET::ActionMinimize;
    }

    // Sticky state not supported
    if (win->isMaximizable()) {
        win->allowed_actions |= NET::ActionMax;
    }
    if (win->userCanSetFullScreen()) {
        win->allowed_actions |= NET::ActionFullScreen;
    }

    // Always (Pagers shouldn't show Docks etc.)
    win->allowed_actions |= NET::ActionChangeDesktop;

    if (win->isCloseable()) {
        win->allowed_actions |= NET::ActionClose;
    }
    if (old_allowed_actions == win->allowed_actions) {
        return;
    }

    // TODO: This could be delayed and compressed - It's only for pagers etc. anyway
    win->info->setAllowedActions(win->allowed_actions);

    // ONLY if relevant features have changed (and the window didn't just get/loose moveresize for
    // maximization state changes)
    auto const relevant = ~(NET::ActionMove | NET::ActionResize);

    if ((win->allowed_actions & relevant) != (old_allowed_actions & relevant)) {
        if ((win->allowed_actions & NET::ActionMinimize)
            != (old_allowed_actions & NET::ActionMinimize)) {
            Q_EMIT win->minimizeableChanged(win->allowed_actions & NET::ActionMinimize);
        }
        if ((win->allowed_actions & NET::ActionMax) != (old_allowed_actions & NET::ActionMax)) {
            Q_EMIT win->maximizeableChanged(win->allowed_actions & NET::ActionMax);
        }
    }
}

}
