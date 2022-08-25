/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "desktop_set.h"
#include "move.h"
#include "rules/book.h"
#include "shortcut_set.h"
#include "stacking.h"

#include "input/cursor.h"

namespace KWin::win
{

template<typename Space>
void perform_window_operation(Space& space,
                              Toplevel* window,
                              base::options_qobject::WindowOperation op)
{
    if (!window) {
        return;
    }

    auto& cursor = space.input->platform.cursor;

    if (op == base::options_qobject::MoveOp || op == base::options_qobject::UnrestrictedMoveOp) {
        cursor->set_pos(window->frameGeometry().center());
    }
    if (op == base::options_qobject::ResizeOp
        || op == base::options_qobject::UnrestrictedResizeOp) {
        cursor->set_pos(window->frameGeometry().bottomRight());
    }

    switch (op) {
    case base::options_qobject::MoveOp:
        window->performMouseCommand(base::options_qobject::MouseMove, cursor->pos());
        break;
    case base::options_qobject::UnrestrictedMoveOp:
        window->performMouseCommand(base::options_qobject::MouseUnrestrictedMove, cursor->pos());
        break;
    case base::options_qobject::ResizeOp:
        window->performMouseCommand(base::options_qobject::MouseResize, cursor->pos());
        break;
    case base::options_qobject::UnrestrictedResizeOp:
        window->performMouseCommand(base::options_qobject::MouseUnrestrictedResize, cursor->pos());
        break;
    case base::options_qobject::CloseOp:
        QMetaObject::invokeMethod(window, "closeWindow", Qt::QueuedConnection);
        break;
    case base::options_qobject::MaximizeOp:
        maximize(window,
                 window->maximizeMode() == maximize_mode::full ? maximize_mode::restore
                                                               : maximize_mode::full);
        break;
    case base::options_qobject::HMaximizeOp:
        maximize(window, window->maximizeMode() ^ maximize_mode::horizontal);
        break;
    case base::options_qobject::VMaximizeOp:
        maximize(window, window->maximizeMode() ^ maximize_mode::vertical);
        break;
    case base::options_qobject::RestoreOp:
        maximize(window, maximize_mode::restore);
        break;
    case base::options_qobject::MinimizeOp:
        set_minimized(window, true);
        break;
    case base::options_qobject::OnAllDesktopsOp:
        set_on_all_desktops(window, !window->isOnAllDesktops());
        break;
    case base::options_qobject::FullScreenOp:
        window->setFullScreen(!window->control->fullscreen(), true);
        break;
    case base::options_qobject::NoBorderOp:
        window->setNoBorder(!window->noBorder());
        break;
    case base::options_qobject::KeepAboveOp: {
        blocker block(space.stacking_order);
        bool was = window->control->keep_above();
        set_keep_above(window, !window->control->keep_above());
        if (was && !window->control->keep_above()) {
            raise_window(&space, window);
        }
        break;
    }
    case base::options_qobject::KeepBelowOp: {
        blocker block(space.stacking_order);
        bool was = window->control->keep_below();
        set_keep_below(window, !window->control->keep_below());
        if (was && !window->control->keep_below()) {
            lower_window(&space, window);
        }
        break;
    }
    case base::options_qobject::WindowRulesOp:
        space.rule_book->edit(window, false);
        break;
    case base::options_qobject::ApplicationRulesOp:
        space.rule_book->edit(window, true);
        break;
    case base::options_qobject::SetupWindowShortcutOp:
        setup_window_shortcut(space, window);
        break;
    case base::options_qobject::LowerOp:
        lower_window(&space, window);
        break;
    case base::options_qobject::OperationsOp:
    case base::options_qobject::NoOp:
        break;
    }
}

}
