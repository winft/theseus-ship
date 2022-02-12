/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "space.h"

#include "rules/rule_book.h"
#include "utils/blocker.h"
#include "win/remnant.h"
#include "win/stacking_order.h"

#ifdef KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

namespace KWin::win::wayland
{

template<typename Win>
void destroy_window(Win* win)
{
    win->closing = true;

    blocker block(workspace()->stacking_order);

    auto remnant_window = win->create_remnant(win);
    Q_EMIT win->windowClosed(win, remnant_window);

    if (win->control) {
#ifdef KWIN_BUILD_TABBOX
        auto tabbox = TabBox::TabBox::self();
        if (tabbox->isDisplayed() && tabbox->currentClient() == win) {
            tabbox->nextPrev(true);
        }
#endif
        if (win->control->move_resize().enabled) {
            win->leaveMoveResize();
        }

        RuleBook::self()->discardUsed(win, true);

        win->control->destroy_wayland_management();
        win->control->destroy_decoration();
    }

    static_cast<win::wayland::space*>(workspace())->handle_window_removed(win);
    remnant_window->remnant()->unref();

    delete win;
}

}
