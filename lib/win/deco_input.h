/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window_operation.h"

#include "base/options.h"

namespace KWin::win
{

template<typename Win>
bool process_decoration_button_press(Win* win, QMouseEvent* event, bool ignoreMenu)
{
    auto com = base::options_qobject::MouseNothing;
    bool active = win->control->active;

    if (!win->wantsInput()) {
        // We cannot be active, use it anyway.
        active = true;
    }

    auto const& qopts = win->space.base.options->qobject;

    // check whether it is a double click
    if (event->button() == Qt::LeftButton && titlebar_positioned_under_mouse(win)) {
        auto& deco = win->control->deco;
        if (deco.double_click.active()) {
            auto const interval = deco.double_click.stop();
            if (interval > QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
                // expired -> new first click and pot. init
                deco.double_click.start();
            } else {
                perform_window_operation(win, qopts->operationTitlebarDblClick());
                end_move_resize(win);
                return false;
            }
        } else {
            // New first click and potential init, could be invalidated by release - see below.
            deco.double_click.start();
        }
    }

    if (event->button() == Qt::LeftButton) {
        com = active ? qopts->commandActiveTitlebar1() : qopts->commandInactiveTitlebar1();
    } else if (event->button() == Qt::MiddleButton) {
        com = active ? qopts->commandActiveTitlebar2() : qopts->commandInactiveTitlebar2();
    } else if (event->button() == Qt::RightButton) {
        com = active ? qopts->commandActiveTitlebar3() : qopts->commandInactiveTitlebar3();
    }

    // Operations menu is for actions where it's not possible to get the matching and
    // mouse minimize for mouse release event.
    if (event->button() == Qt::LeftButton && com != base::options_qobject::MouseOperationsMenu
        && com != base::options_qobject::MouseMinimize) {
        auto& mov_res = win->control->move_resize;

        mov_res.contact = win::mouse_position(win);
        mov_res.button_down = true;
        mov_res.offset = event->pos();

        // TODO: use win's size instead.
        mov_res.inverted_offset
            = QPoint(win->geo.size().width() - 1, win->geo.size().height() - 1) - mov_res.offset;
        mov_res.unrestricted = false;
        win::start_delayed_move_resize(win);
        win::update_cursor(win);
    }
    // In the new API the decoration may process the menu action to display an inactive tab's menu.
    // If the event is unhandled then the core will create one for the active window in the group.
    if (!ignoreMenu || com != base::options_qobject::MouseOperationsMenu) {
        perform_mouse_command(*win, com, event->globalPos());
    }

    // Return events that should be passed to the decoration in the new API.
    return !(com == base::options_qobject::MouseRaise
             || com == base::options_qobject::MouseOperationsMenu
             || com == base::options_qobject::MouseActivateAndRaise
             || com == base::options_qobject::MouseActivate
             || com == base::options_qobject::MouseActivateRaiseAndPassClick
             || com == base::options_qobject::MouseActivateAndPassClick
             || com == base::options_qobject::MouseNothing);
}

}
