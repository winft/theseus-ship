/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "main.h"
#include "win/geo.h"
#include "win/wayland/input.h"
#include "win/x11/stacking.h"

namespace KWin::input
{

template<typename Redirect>
auto find_window(Redirect const& redirect, QPoint const& pos) -> typename Redirect::window_t*
{
    // TODO: check whether the unmanaged wants input events at all
    if (!kwinApp()->is_screen_locked()) {
        // if an effect overrides the cursor we don't have a window to focus
        if (redirect.platform.base.render->compositor->effects
            && redirect.platform.base.render->compositor->effects->isMouseInterception()) {
            return nullptr;
        }

        auto const& unmanaged = win::x11::get_unmanageds(redirect.space);
        for (auto const& u : unmanaged) {
            if (win::input_geometry(u).contains(pos) && win::wayland::accepts_input(u, pos)) {
                return u;
            }
        }
    }

    return find_controlled_window(redirect, pos);
}

template<typename Redirect>
auto find_controlled_window(Redirect const& redirect, QPoint const& pos) ->
    typename Redirect::window_t*
{
    auto const isScreenLocked = kwinApp()->is_screen_locked();
    auto const& stacking = redirect.space.stacking.order.stack;
    if (stacking.empty()) {
        return nullptr;
    }

    auto it = stacking.end();

    do {
        --it;
        auto window = *it;
        if (window->remnant) {
            // a deleted window doesn't get mouse events
            continue;
        }
        if (window->control) {
            if (!window->isOnCurrentDesktop() || window->control->minimized) {
                continue;
            }
        }
        if (window->isHiddenInternal()) {
            continue;
        }
        if (!window->ready_for_painting) {
            continue;
        }
        if (isScreenLocked) {
            if (!window->isLockScreen() && !window->isInputMethod()) {
                continue;
            }
        }
        if (win::input_geometry(window).contains(pos) && win::wayland::accepts_input(window, pos)) {
            return window;
        }
    } while (it != stacking.begin());

    return nullptr;
}

}
