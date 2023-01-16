/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "group.h"

#include "base/options.h"
#include "main.h"
#include "win/meta.h"

namespace KWin::win::x11
{

template<typename Space>
void reset_update_tool_windows_timer(Space& space)
{
    space.updateToolWindowsTimer.start(200);
}

/**
 * Relevant for windows of type NET::Utility, NET::Menu or NET::Toolbar.
 */
template<typename Space>
void update_tool_windows_visibility(Space* space, bool also_hide)
{
    if (!space->base.options->qobject->isHideUtilityWindowsForInactive()) {
        for (auto&& window : space->windows) {
            std::visit(overload{[](auto&& window) {
                           if (window->control) {
                               window->hideClient(false);
                           }
                       }},
                       window);
        }
        return;
    }

    using x11_window_t = typename Space::x11_window;
    x11::group<Space> const* active_group = nullptr;
    x11_window_t* active_window{nullptr};

    if (auto& active = space->stacking.active;
        active.has_value() && std::holds_alternative<x11_window_t*>(*active)) {
        active_window = std::get<x11_window_t*>(*active);
    }

    // Go up in transiency hiearchy, if the top is found, only tool transients for the top
    // window will be shown; if a group transient is group, all tools in the group will be shown.
    while (active_window) {
        if (!active_window->transient->lead()) {
            break;
        }
        if (active_window->groupTransient()) {
            active_group = active_window->group;
            break;
        }
        active_window = static_cast<x11_window_t*>(active_window->transient->lead());
    }

    // Use stacking order only to reduce flicker, it doesn't matter if block_stacking_updates == 0,
    // i.e. if it's not up to date.

    // TODO(SELI): But maybe it should - what if a new window has been added that's not in stacking
    // order yet?
    std::vector<x11_window_t*> to_show;
    std::vector<x11_window_t*> to_hide;

    for (auto const& win : space->stacking.order.stack) {
        if (!std::holds_alternative<x11_window_t*>(win)) {
            continue;
        }
        auto x11_win = std::get<x11_window_t*>(win);
        if (!x11_win->control) {
            continue;
        }

        if (!is_utility(x11_win) && !is_menu(x11_win) && !is_toolbar(x11_win)) {
            continue;
        }

        auto show{true};

        if (x11_win->transient->lead()) {
            auto const in_active_group = active_group && x11_win->group == active_group;
            auto const has_active_lead
                = active_window && x11_win->transient->is_follower_of(active_window);
            show = in_active_group || has_active_lead;
        } else {
            auto const is_individual = !x11_win->group || x11_win->group->members.size() == 1;
            auto const in_active_group = active_window && active_window->group == x11_win->group;
            show = is_individual || in_active_group;
        }

        if (!show && also_hide) {
            auto const& leads = x11_win->transient->leads();
            // Don't hide utility windows which are standalone(?) or have e.g. kicker as lead.
            show = leads.empty()
                || std::any_of(leads.cbegin(), leads.cend(), is_special_window<x11_window_t>);
            if (!show) {
                to_hide.push_back(x11_win);
            }
        }

        if (show) {
            to_show.push_back(x11_win);
        }
    }

    // First show new ones, then hide.
    // Show from topmost.
    for (int i = to_show.size() - 1; i >= 0; --i) {
        // TODO(unknown author): Since this is in stacking order, the order of taskbar entries
        //                       changes :(
        to_show.at(i)->hideClient(false);
    }

    if (also_hide) {
        // Hide from bottom-most.
        for (auto const& win : to_hide) {
            win->hideClient(true);
        }
        space->updateToolWindowsTimer.stop();
    } else {
        // Workspace::setActiveClient(..) is afterwards called with NULL client, quickly followed
        // by setting a new client, which would result in flickering.
        reset_update_tool_windows_timer(*space);
    }
}

}
