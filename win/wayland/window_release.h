/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"

#include "utils/blocker.h"
#include "win/remnant.h"
#include "win/rules/book.h"
#include "win/stacking_order.h"
#include "win/transient.h"
#include "win/window_release.h"

#if KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

namespace KWin::win::wayland
{

template<typename Win>
void destroy_window(Win* win)
{
    blocker block(win->space.stacking_order);
    win->closing = true;

    if (win->transient()->annexed && !lead_of_annexed_transient(win)) {
        // With the lead gone there is no way - and no need - for remnant effects. Delete directly.
        Q_EMIT win->qobject->closed();
        win->space.handle_window_removed(win);
        remove_all(win->space.windows, win);
        remove_all(win->space.stacking_order->pre_stack, win);
        remove_all(win->space.stacking_order->stack, win);
        delete win;
        return;
    }

    auto remnant_window = create_remnant_window<Win>(*win);
    Q_EMIT win->qobject->closed();

    if (win->control) {
#if KWIN_BUILD_TABBOX
        auto& tabbox = win->space.tabbox;
        if (tabbox->is_displayed() && tabbox->current_client() == win) {
            tabbox->next_prev(true);
        }
#endif
        if (win->control->move_resize().enabled) {
            win->leaveMoveResize();
        }

        win->space.rule_book->discardUsed(win, true);

        win->control->destroy_plasma_wayland_integration();
        win->control->destroy_decoration();
    }

    win->space.handle_window_removed(win);

    if (remnant_window) {
        remnant_window->remnant->unref();
        delete win;
    } else {
        delete_window_from_space(win->space, win);
    }
}

}
