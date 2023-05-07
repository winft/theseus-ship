/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "base/options.h"
#include "desktop_set.h"
#include "move.h"
#include "rules/book_edit.h"
#include "shortcut_set.h"
#include "stacking.h"

#include "input/cursor.h"

namespace KWin::win
{

template<typename Win>
void perform_window_operation(Win* window, win_op op)
{
    if (!window) {
        return;
    }

    auto& space = window->space;
    auto& cursor = space.input->cursor;

    if (op == win_op::move || op == win_op::unrestricted_move) {
        cursor->set_pos(window->geo.frame.center());
    }
    if (op == win_op::resize || op == win_op::unrestricted_resize) {
        cursor->set_pos(window->geo.frame.bottomRight());
    }

    switch (op) {
    case win_op::move:
        perform_mouse_command(*window, mouse_cmd::move, cursor->pos());
        break;
    case win_op::unrestricted_move:
        perform_mouse_command(*window, mouse_cmd::unrestricted_move, cursor->pos());
        break;
    case win_op::resize:
        perform_mouse_command(*window, mouse_cmd::resize, cursor->pos());
        break;
    case win_op::unrestricted_resize:
        perform_mouse_command(*window, mouse_cmd::unrestricted_resize, cursor->pos());
        break;
    case win_op::close:
        QMetaObject::invokeMethod(
            window->qobject.get(), [window] { window->closeWindow(); }, Qt::QueuedConnection);
        break;
    case win_op::maximize:
        maximize(window,
                 window->maximizeMode() == maximize_mode::full ? maximize_mode::restore
                                                               : maximize_mode::full);
        break;
    case win_op::h_maximize:
        maximize(window, window->maximizeMode() ^ maximize_mode::horizontal);
        break;
    case win_op::v_maximize:
        maximize(window, window->maximizeMode() ^ maximize_mode::vertical);
        break;
    case win_op::restore:
        maximize(window, maximize_mode::restore);
        break;
    case win_op::minimize:
        set_minimized(window, true);
        break;
    case win_op::on_all_desktops:
        set_on_all_desktops(window, !on_all_desktops(window));
        break;
    case win_op::fullscreen:
        window->setFullScreen(!window->control->fullscreen, true);
        break;
    case win_op::no_border:
        window->setNoBorder(!window->noBorder());
        break;
    case win_op::keep_above: {
        blocker block(space.stacking.order);
        bool was = window->control->keep_above;
        set_keep_above(window, !window->control->keep_above);
        if (was && !window->control->keep_above) {
            raise_window(space, window);
        }
        break;
    }
    case win_op::keep_below: {
        blocker block(space.stacking.order);
        bool was = window->control->keep_below;
        set_keep_below(window, !window->control->keep_below);
        if (was && !window->control->keep_below) {
            lower_window(space, window);
        }
        break;
    }
    case win_op::window_rules:
        rules::edit_book(*space.rule_book, *window, false);
        break;
    case win_op::application_rules:
        rules::edit_book(*space.rule_book, *window, true);
        break;
    case win_op::setup_window_shortcut:
        shortcut_dialog_create(space, window);
        break;
    case win_op::lower:
        lower_window(space, window);
        break;
    case win_op::operations:
    case win_op::noop:
        break;
    }
}

}
