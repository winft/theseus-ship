/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "rules/find.h"
#include "x11/hide.h"
#include "x11/tool_windows.h"

#include "main.h"

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

    bool borderlessMaximizedWindows = kwinApp()->options->qobject->borderlessMaximizedWindows();

    kwinApp()->config()->reparseConfiguration();
    kwinApp()->options->updateSettings();
    space.scripting->start();

    Q_EMIT space.qobject->configChanged();

    space.user_actions_menu->discard();
    x11::update_tool_windows_visibility(&space, true);

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

    if (borderlessMaximizedWindows != kwinApp()->options->qobject->borderlessMaximizedWindows()
        && !kwinApp()->options->qobject->borderlessMaximizedWindows()) {
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
