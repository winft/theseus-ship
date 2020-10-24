/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "rules/rules.h"
#include "workspace.h"

namespace KWin::win
{

template<typename Win>
void auto_raise(Win* win)
{
    workspace()->raiseClient(dynamic_cast<AbstractClient*>(win));
    win->control()->cancel_auto_raise();
}

template<typename Win>
void set_keep_below(Win* win, bool keep);

template<typename Win>
void set_keep_above(Win* win, bool keep)
{
    keep = win->rules()->checkKeepAbove(keep);
    if (keep && !win->rules()->checkKeepBelow(false)) {
        set_keep_below(win, false);
    }
    if (keep == win->control()->keep_above()) {
        // force hint change if different
        if (win->info && bool(win->info->state() & NET::KeepAbove) != keep) {
            win->info->setState(keep ? NET::KeepAbove : NET::States(), NET::KeepAbove);
        }
        return;
    }
    win->control()->set_keep_above(keep);
    if (win->info) {
        win->info->setState(keep ? NET::KeepAbove : NET::States(), NET::KeepAbove);
    }
    workspace()->updateClientLayer(win);
    win->updateWindowRules(Rules::Above);

    win->doSetKeepAbove();
    Q_EMIT win->keepAboveChanged(keep);
}

template<typename Win>
void set_keep_below(Win* win, bool keep)
{
    keep = win->rules()->checkKeepBelow(keep);
    if (keep && !win->rules()->checkKeepAbove(false)) {
        set_keep_above(win, false);
    }
    if (keep == win->control()->keep_below()) {
        // force hint change if different
        if (win->info && bool(win->info->state() & NET::KeepBelow) != keep)
            win->info->setState(keep ? NET::KeepBelow : NET::States(), NET::KeepBelow);
        return;
    }
    win->control()->set_keep_below(keep);
    if (win->info) {
        win->info->setState(keep ? NET::KeepBelow : NET::States(), NET::KeepBelow);
    }
    workspace()->updateClientLayer(win);
    win->updateWindowRules(Rules::Below);

    win->doSetKeepBelow();
    Q_EMIT win->keepBelowChanged(keep);
}

template<typename Win>
void set_demands_attention(Win* win, bool demand)
{
    if (win->control()->active()) {
        demand = false;
    }
    if (win->control()->demands_attention() == demand) {
        return;
    }
    win->control()->set_demands_attention(demand);

    if (win->info) {
        win->info->setState(demand ? NET::DemandsAttention : NET::States(), NET::DemandsAttention);
    }

    workspace()->clientAttentionChanged(win, demand);
    Q_EMIT win->demandsAttentionChanged();
}

}
