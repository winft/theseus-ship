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
auto find_window(Redirect const& redirect, QPoint const& pos)
    -> std::optional<typename Redirect::window_t>
{
    // TODO: check whether the unmanaged wants input events at all
    if (!kwinApp()->is_screen_locked()) {
        // if an effect overrides the cursor we don't have a window to focus
        if (redirect.platform.base.render->compositor->effects
            && redirect.platform.base.render->compositor->effects->isMouseInterception()) {
            return {};
        }

        auto const& unmanaged = win::x11::get_unmanageds(redirect.space);
        for (auto const& win : unmanaged) {
            if (std::visit(overload{[&](auto&& win) {
                               return win::input_geometry(win).contains(pos)
                                   && win::wayland::accepts_input(win, pos);
                           }},
                           win)) {
                return win;
            }
        }
    }

    return find_controlled_window(redirect, pos);
}

template<typename Redirect>
auto find_controlled_window(Redirect const& redirect, QPoint const& pos)
    -> std::optional<typename Redirect::window_t>
{
    auto const isScreenLocked = kwinApp()->is_screen_locked();
    auto const& stacking = redirect.space.stacking.order.stack;
    if (stacking.empty()) {
        return {};
    }

    auto it = stacking.end();

    do {
        --it;

        if (std::visit(overload{[&](auto&& win) {
                           if (win->remnant) {
                               // a deleted window doesn't get mouse events
                               return false;
                           }
                           if (win->control) {
                               if (!win::on_current_desktop(win) || win->control->minimized) {
                                   return false;
                               }
                           }
                           if (win->isHiddenInternal()) {
                               return false;
                           }
                           if (!win->render_data.ready_for_painting) {
                               return false;
                           }
                           if (isScreenLocked) {
                               auto show{false};
                               using win_t = decltype(win);

                               if constexpr (requires(win_t win) { win->isLockScreen(); }) {
                                   show |= win->isLockScreen();
                               }
                               if constexpr (requires(win_t win) { win->isInputMethod(); }) {
                                   show |= win->isInputMethod();
                               }
                               if (!show) {
                                   return false;
                               }
                           }
                           return win::input_geometry(win).contains(pos)
                               && win::wayland::accepts_input(win, pos);
                       }},
                       *it)) {
            return *it;
        }
    } while (it != stacking.begin());

    return {};
}
}
