/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"

#include "space.h"

#include "rules/rule_book.h"
#include "utils/blocker.h"
#include "win/remnant.h"
#include "win/stacking_order.h"
#include "win/transient.h"
#include "win/x11/stacking_tree.h"

#if KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

namespace KWin::win::wayland
{

template<typename Win>
void destroy_window(Win* win)
{
    auto space = static_cast<win::wayland::space*>(workspace());
    blocker block(space->stacking_order);
    win->closing = true;

    if (win->transient()->annexed && !lead_of_annexed_transient(win)) {
        // With the lead gone there is no way - and no need - for remnant effects. Delete directly.
        Q_EMIT win->windowClosed(win, nullptr);
        space->handle_window_removed(win);
        space->x_stacking_tree->mark_as_dirty();
        remove_all(space->m_windows, win);
        remove_all(space->stacking_order->pre_stack, win);
        remove_all(space->stacking_order->win_stack, win);
        delete win;
        return;
    }

    auto remnant_window = win->create_remnant(win);
    Q_EMIT win->windowClosed(win, remnant_window);

    if (win->control) {
#if KWIN_BUILD_TABBOX
        auto tabbox = tabbox::tabbox::self();
        if (tabbox->is_displayed() && tabbox->current_client() == win) {
            tabbox->next_prev(true);
        }
#endif
        if (win->control->move_resize().enabled) {
            win->leaveMoveResize();
        }

        RuleBook::self()->discardUsed(win, true);

        win->control->destroy_wayland_management();
        win->control->destroy_decoration();
    }

    space->handle_window_removed(win);
    remnant_window->remnant()->unref();

    delete win;
}

}
