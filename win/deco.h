/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_DECO_H
#define KWIN_WIN_DECO_H

#include "move.h"
#include "workspace.h"

#include <KDecoration2/Decoration>

namespace KWin::win
{

/**
 * Request showing the application menu bar.
 * @param actionId The DBus menu ID of the action that should be highlighted, 0 for the root menu.
 */
template<typename Win>
void show_application_menu(Win* win, int actionId)
{
    if (win->isDecorated()) {
        win->decoration()->showApplicationMenu(actionId);
    } else {
        // No info where application menu button is, show it in the top left corner by default.
        Workspace::self()->showApplicationMenu(QRect(), win, actionId);
    }
}

template<typename Win>
bool decoration_has_alpha(Win* win)
{
    return win->isDecorated() && !win->decoration()->isOpaque();
}

template<typename Win>
void trigger_decoration_repaint(Win* win)
{
    if (win->isDecorated()) {
        win->decoration()->update();
    }
}

template<typename Win>
void layout_decoration_rects(Win* win, QRect& left, QRect& top, QRect& right, QRect& bottom)
{
    if (!win->isDecorated()) {
        return;
    }
    auto rect = win->decoration()->rect();

    top = QRect(rect.x(), rect.y(), rect.width(), top_border(win));
    bottom = QRect(
        rect.x(), rect.y() + rect.height() - bottom_border(win), rect.width(), bottom_border(win));
    left = QRect(rect.x(),
                 rect.y() + top.height(),
                 left_border(win),
                 rect.height() - top.height() - bottom.height());
    right = QRect(rect.x() + rect.width() - right_border(win),
                  rect.y() + top.height(),
                  right_border(win),
                  rect.height() - top.height() - bottom.height());
}

}

#endif
