/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "rules/find.h"

namespace KWin::win
{

template<typename Space>
void space_start_reconfigure_timer(Space& space)
{
    space.reconfigureTimer.start(200);
}

template<typename Space>
void space_reconfigure(Space& space)
{
    space.reconfigureTimer.stop();

    bool borderlessMaximizedWindows = space.options->qobject->borderlessMaximizedWindows();

    space.base.config.main->reparseConfiguration();
    space.options->updateSettings();
    space.base.options->updateSettings();

    if constexpr (requires(decltype(space) space) { space.base.mod.script; }) {
        space.base.mod.script->start();
    }

    Q_EMIT space.qobject->configChanged();

    space.user_actions_menu->discard();

    if constexpr (requires(Space space, bool arg) { space.update_tool_windows_visibility(arg); }) {
        space.update_tool_windows_visibility(true);
    }

    space.rule_book->load();
    for (auto win : space.windows) {
        std::visit(overload{[&](auto&& win) {
                       if (win->supportsWindowRules()) {
                           rules::evaluate_rules(win);
                           rules::discard_used_rules(*space.rule_book, *win, false);
                       }
                   }},
                   win);
    }

    if (borderlessMaximizedWindows != space.options->qobject->borderlessMaximizedWindows()
        && !space.options->qobject->borderlessMaximizedWindows()) {
        // in case borderless maximized windows option changed and new option
        // is to have borders, we need to unset the borders for all maximized windows
        for (auto win : space.windows) {
            std::visit(overload{[&](auto&& win) {
                           if (win->maximizeMode() == maximize_mode::full) {
                               win->checkNoBorder();
                           }
                       }},
                       win);
        }
    }
}

}
