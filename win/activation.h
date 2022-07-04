/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "layers.h"
#include "stacking_order.h"

#include "utils/blocker.h"

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

/**
 * Sets the client's active state to \a act.
 *
 * This function does only change the visual appearance of the client,
 * it does not change the focus setting. Use
 * Workspace::activateClient() or Workspace::requestFocus() instead.
 *
 * If a client receives or looses the focus, it calls setActive() on
 * its own.
 */
template<typename Win>
void set_active(Win* win, bool active)
{
    if (win->control->active() == active) {
        return;
    }
    win->control->set_active(active);

    auto const ruledOpacity = active
        ? win->control->rules().checkOpacityActive(qRound(win->opacity() * 100.0))
        : win->control->rules().checkOpacityInactive(qRound(win->opacity() * 100.0));
    win->setOpacity(ruledOpacity / 100.0);

    win->space.setActiveClient(active ? win : nullptr);

    if (!active) {
        win->control->cancel_auto_raise();
    }

    blocker block(win->space.stacking_order);

    // active windows may get different layer
    update_layer(win);

    auto leads = win->transient()->leads();
    for (auto lead : leads) {
        if (lead->remnant) {
            continue;
        }
        if (lead->control->fullscreen()) {
            // Fullscreens go high even if their transient is active.
            update_layer(lead);
        }
    }

    win->doSetActive();
    Q_EMIT win->activeChanged();
    win->control->update_mouse_grab();
}

}
