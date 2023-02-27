/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net/net.h"

namespace KWin::win::x11
{

template<typename Win>
void update_allowed_actions(Win* win, bool force = false)
{
    if (!win->control && !force) {
        return;
    }

    auto old_allowed_actions = net::Actions(win->allowed_actions);
    win->allowed_actions = net::Actions();

    if (win->isMovable()) {
        win->allowed_actions |= net::ActionMove;
    }
    if (win->isResizable()) {
        win->allowed_actions |= net::ActionResize;
    }
    if (win->isMinimizable()) {
        win->allowed_actions |= net::ActionMinimize;
    }

    // Sticky state not supported
    if (win->isMaximizable()) {
        win->allowed_actions |= net::ActionMax;
    }
    if (win->userCanSetFullScreen()) {
        win->allowed_actions |= net::ActionFullScreen;
    }

    // Always (Pagers shouldn't show Docks etc.)
    win->allowed_actions |= net::ActionChangeDesktop;

    if (win->isCloseable()) {
        win->allowed_actions |= net::ActionClose;
    }
    if (old_allowed_actions == win->allowed_actions) {
        return;
    }

    // TODO: This could be delayed and compressed - It's only for pagers etc. anyway
    win->net_info->setAllowedActions(win->allowed_actions);

    // ONLY if relevant features have changed (and the window didn't just get/loose moveresize for
    // maximization state changes)
    auto const relevant = ~(net::ActionMove | net::ActionResize);

    if ((win->allowed_actions & relevant) != (old_allowed_actions & relevant)) {
        if ((win->allowed_actions & net::ActionMinimize)
            != (old_allowed_actions & net::ActionMinimize)) {
            Q_EMIT win->qobject->minimizeableChanged(win->allowed_actions & net::ActionMinimize);
        }
        if ((win->allowed_actions & net::ActionMax) != (old_allowed_actions & net::ActionMax)) {
            Q_EMIT win->qobject->maximizeableChanged(win->allowed_actions & net::ActionMax);
        }
    }
}

}
