/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "input.h"

#include "focuschain.h"
#include "rules/rules.h"

namespace KWin
{
namespace win
{

template<typename Win>
void set_skip_pager(Win* win, bool set)
{
    set = win->rules()->checkSkipPager(set);
    if (set == win->control()->skip_pager()) {
        return;
    }

    win->control()->set_skip_pager(set);
    win->updateWindowRules(Rules::SkipPager);
    Q_EMIT win->skipPagerChanged();
}

template<typename Win>
void set_skip_switcher(Win* win, bool set)
{
    set = win->rules()->checkSkipSwitcher(set);
    if (set == win->control()->skip_switcher()) {
        return;
    }

    win->control()->set_skip_switcher(set);
    win->updateWindowRules(Rules::SkipSwitcher);
    Q_EMIT win->skipSwitcherChanged();
}

template<typename Win>
void set_skip_taskbar(Win* win, bool set)
{
    if (set == win->control()->skip_taskbar()) {
        return;
    }

    auto const was_wants_tab_focus = win::wants_tab_focus(win);

    win->control()->set_skip_taskbar(set);
    win->updateWindowRules(Rules::SkipTaskbar);

    if (was_wants_tab_focus != win::wants_tab_focus(win)) {
        FocusChain::self()->update(win,
                                   win->isActive() ? FocusChain::MakeFirst : FocusChain::Update);
    }

    Q_EMIT win->skipTaskbarChanged();
}

template<typename Win>
void set_original_skip_taskbar(Win* win, bool set)
{
    auto const rules_checked = win->rules()->checkSkipTaskbar(set);
    win->control()->set_original_skip_taskbar(rules_checked);
    win::set_skip_taskbar(win, rules_checked);
}

}
}
