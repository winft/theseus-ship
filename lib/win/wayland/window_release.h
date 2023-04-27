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
    using var_win = typename Win::space_t::window_t;

    blocker block(win->space.stacking.order);
    win->closing = true;

    if (win->transient->annexed && !lead_of_annexed_transient(win)) {
        // With the lead gone there is no way - and no need - for remnant effects. Delete directly.
        Q_EMIT win->qobject->closed();
        win->space.handle_window_removed(win);
        remove_all(win->space.windows, var_win(win));
        remove_all(win->space.stacking.order.pre_stack, var_win(win));
        remove_all(win->space.stacking.order.stack, var_win(win));
        delete win;
        return;
    }

    auto remnant_window = create_remnant_window<Win>(*win);
    if (remnant_window) {
        transfer_remnant_data(*win, *remnant_window);
        space_add_remnant(*win, *remnant_window);
        scene_add_remnant(*remnant_window);
    }
    Q_EMIT win->qobject->closed();

    if (win->control) {
#if KWIN_BUILD_TABBOX
        auto& tabbox = win->space.tabbox;
        if (tabbox->is_displayed() && tabbox->current_client()
            && *tabbox->current_client() == var_win(win)) {
            tabbox->next_prev(true);
        }
#endif
        if (win->control->move_resize.enabled) {
            static_assert(!requires(Win win) { win.leaveMoveResize(); });
            win::leave_move_resize(*win);
        }

        rules::discard_used_rules(*win->space.rule_book, *win, true);

        win->control->destroy_plasma_wayland_integration();
        win->control->destroy_decoration();
    }

    win->space.handle_window_removed(win);

    if (remnant_window) {
        remnant_window->remnant->unref();
        delete win;
    } else {
        delete_window_from_space(win->space, *win);
    }
}

}
