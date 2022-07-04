/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::win
{

template<typename Win>
void set_demands_attention(Win* win, bool demand)
{
    if (win->control->active()) {
        demand = false;
    }
    if (win->control->demands_attention() == demand) {
        return;
    }
    win->control->set_demands_attention(demand);

    if (win->info) {
        win->info->setState(demand ? NET::DemandsAttention : NET::States(), NET::DemandsAttention);
    }

    win->space.clientAttentionChanged(win, demand);
    Q_EMIT win->demandsAttentionChanged();
}

}
