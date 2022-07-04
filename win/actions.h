/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "layers.h"
#include "scene.h"

namespace KWin::win
{

template<typename Win>
void set_keep_below(Win* win, bool keep);

template<typename Win>
void set_keep_above(Win* win, bool keep)
{
    keep = win->control->rules().checkKeepAbove(keep);
    if (keep && !win->control->rules().checkKeepBelow(false)) {
        set_keep_below(win, false);
    }
    if (keep == win->control->keep_above()) {
        // force hint change if different
        if (win->info && bool(win->info->state() & NET::KeepAbove) != keep) {
            win->info->setState(keep ? NET::KeepAbove : NET::States(), NET::KeepAbove);
        }
        return;
    }
    win->control->set_keep_above(keep);
    if (win->info) {
        win->info->setState(keep ? NET::KeepAbove : NET::States(), NET::KeepAbove);
    }
    update_layer(win);
    win->updateWindowRules(Rules::Above);

    win->doSetKeepAbove();
    Q_EMIT win->keepAboveChanged(keep);
}

template<typename Win>
void set_keep_below(Win* win, bool keep)
{
    keep = win->control->rules().checkKeepBelow(keep);
    if (keep && !win->control->rules().checkKeepAbove(false)) {
        set_keep_above(win, false);
    }
    if (keep == win->control->keep_below()) {
        // force hint change if different
        if (win->info && bool(win->info->state() & NET::KeepBelow) != keep)
            win->info->setState(keep ? NET::KeepBelow : NET::States(), NET::KeepBelow);
        return;
    }
    win->control->set_keep_below(keep);
    if (win->info) {
        win->info->setState(keep ? NET::KeepBelow : NET::States(), NET::KeepBelow);
    }
    update_layer(win);
    win->updateWindowRules(Rules::Below);

    win->doSetKeepBelow();
    Q_EMIT win->keepBelowChanged(keep);
}

template<typename Win>
void set_minimized(Win* win, bool set, bool avoid_animation = false)
{
    if (set) {
        if (!win->isMinimizable() || win->control->minimized())
            return;

        win->control->set_minimized(true);
        win->doMinimize();

        win->updateWindowRules(Rules::Minimize);
        win->space.render.addRepaint(visible_rect(win));

        // TODO: merge signal with s_minimized
        Q_EMIT win->clientMinimized(win, !avoid_animation);
        Q_EMIT win->minimizedChanged();
    } else {
        if (!win->control->minimized()) {
            return;
        }
        if (win->control->rules().checkMinimize(false)) {
            return;
        }

        win->control->set_minimized(false);
        win->doMinimize();

        win->updateWindowRules(Rules::Minimize);
        Q_EMIT win->clientUnminimized(win, !avoid_animation);
        Q_EMIT win->minimizedChanged();
    }
}

}
