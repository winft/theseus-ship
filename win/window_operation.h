/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "desktop_set.h"
#include "move.h"
#include "rules/rule_book.h"
#include "shortcut_set.h"
#include "stacking.h"

#include "input/cursor.h"

namespace KWin::win
{

template<typename Space>
void perform_window_operation(Space& space, Toplevel* window, base::options::WindowOperation op)
{
    if (!window) {
        return;
    }

    auto cursor = input::get_cursor();

    if (op == base::options::MoveOp || op == base::options::UnrestrictedMoveOp) {
        cursor->set_pos(window->frameGeometry().center());
    }
    if (op == base::options::ResizeOp || op == base::options::UnrestrictedResizeOp) {
        cursor->set_pos(window->frameGeometry().bottomRight());
    }

    switch (op) {
    case base::options::MoveOp:
        window->performMouseCommand(base::options::MouseMove, cursor->pos());
        break;
    case base::options::UnrestrictedMoveOp:
        window->performMouseCommand(base::options::MouseUnrestrictedMove, cursor->pos());
        break;
    case base::options::ResizeOp:
        window->performMouseCommand(base::options::MouseResize, cursor->pos());
        break;
    case base::options::UnrestrictedResizeOp:
        window->performMouseCommand(base::options::MouseUnrestrictedResize, cursor->pos());
        break;
    case base::options::CloseOp:
        QMetaObject::invokeMethod(window, "closeWindow", Qt::QueuedConnection);
        break;
    case base::options::MaximizeOp:
        maximize(window,
                 window->maximizeMode() == maximize_mode::full ? maximize_mode::restore
                                                               : maximize_mode::full);
        break;
    case base::options::HMaximizeOp:
        maximize(window, window->maximizeMode() ^ maximize_mode::horizontal);
        break;
    case base::options::VMaximizeOp:
        maximize(window, window->maximizeMode() ^ maximize_mode::vertical);
        break;
    case base::options::RestoreOp:
        maximize(window, maximize_mode::restore);
        break;
    case base::options::MinimizeOp:
        set_minimized(window, true);
        break;
    case base::options::OnAllDesktopsOp:
        set_on_all_desktops(window, !window->isOnAllDesktops());
        break;
    case base::options::FullScreenOp:
        window->setFullScreen(!window->control->fullscreen(), true);
        break;
    case base::options::NoBorderOp:
        window->setNoBorder(!window->noBorder());
        break;
    case base::options::KeepAboveOp: {
        blocker block(space.stacking_order);
        bool was = window->control->keep_above();
        set_keep_above(window, !window->control->keep_above());
        if (was && !window->control->keep_above()) {
            raise_window(&space, window);
        }
        break;
    }
    case base::options::KeepBelowOp: {
        blocker block(space.stacking_order);
        bool was = window->control->keep_below();
        set_keep_below(window, !window->control->keep_below());
        if (was && !window->control->keep_below()) {
            lower_window(&space, window);
        }
        break;
    }
    case base::options::WindowRulesOp:
        space.rule_book->edit(window, false);
        break;
    case base::options::ApplicationRulesOp:
        space.rule_book->edit(window, true);
        break;
    case base::options::SetupWindowShortcutOp:
        setup_window_shortcut(space, window);
        break;
    case base::options::LowerOp:
        lower_window(&space, window);
        break;
    case base::options::OperationsOp:
    case base::options::NoOp:
        break;
    }
}

}
