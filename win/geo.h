/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_GEO_H
#define KWIN_WIN_GEO_H

#include "types.h"

#include "workspace.h"

#include <QRect>

namespace KWin::win
{

/**
 * Calculates the matching client rect for the given frame rect @p rect.
 *
 * Notice that size constraints won't be applied.
 */
template<typename Win>
QRect frame_rect_to_client_rect(Win* win, QRect const& rect)
{
    auto const position = win->framePosToClientPos(rect.topLeft());
    auto const size = win->frameSizeToClientSize(rect.size());
    return QRect(position, size);
}

/**
 * Calculates the matching frame rect for the given client rect @p rect.
 *
 * Notice that size constraints won't be applied.
 */
template<typename Win>
QRect client_rect_to_frame_rect(Win* win, QRect const& rect)
{
    auto const position = win->clientPosToFramePos(rect.topLeft());
    auto const size = win->clientSizeToFrameSize(rect.size());
    return QRect(position, size);
}

template<typename Win>
void grow_horizontal(Win* win)
{
    if (!win->isResizable() || win->isShade()) {
        return;
    }

    auto geom = win->frameGeometry();
    geom.setRight(workspace()->packPositionRight(win, geom.right(), true));
    auto adjsize = adjusted_size(win, geom.size(), size_mode::fixed_width);
    if (win->frameGeometry().size() == adjsize && geom.size() != adjsize
        && win->resizeIncrements().width() > 1) {
        // take care of size increments
        auto const newright = workspace()->packPositionRight(
            win, geom.right() + win->resizeIncrements().width() - 1, true);

        // check that it hasn't grown outside of the area, due to size increments
        // TODO this may be wrong?
        auto const area = workspace()->clientArea(
            MovementArea,
            QPoint((win->x() + newright) / 2, win->frameGeometry().center().y()),
            win->desktop());
        if (area.right() >= newright) {
            geom.setRight(newright);
        }
    }

    geom.setSize(adjusted_size(win, geom.size(), size_mode::fixed_width));
    geom.setSize(adjusted_size(win, geom.size(), size_mode::fixed_height));
    workspace()->updateFocusMousePosition(Cursor::pos()); // may cause leave event;
    win->setFrameGeometry(geom);
}

template<typename Win>
void shrink_horizontal(Win* win)
{
    if (!win->isResizable() || win->isShade()) {
        return;
    }

    auto geom = win->frameGeometry();
    geom.setRight(workspace()->packPositionLeft(win, geom.right(), false));

    if (geom.width() <= 1) {
        return;
    }

    geom.setSize(adjusted_size(win, geom.size(), size_mode::fixed_width));
    if (geom.width() > 20) {
        workspace()->updateFocusMousePosition(Cursor::pos()); // may cause leave event;
        win->setFrameGeometry(geom);
    }
}

template<typename Win>
void grow_vertical(Win* win)
{
    if (!win->isResizable() || win->isShade()) {
        return;
    }

    auto geom = win->frameGeometry();
    geom.setBottom(workspace()->packPositionDown(win, geom.bottom(), true));
    auto adjsize = adjusted_size(win, geom.size(), size_mode::fixed_height);

    if (win->frameGeometry().size() == adjsize && geom.size() != adjsize
        && win->resizeIncrements().height() > 1) {
        // take care of size increments
        auto const newbottom = workspace()->packPositionDown(
            win, geom.bottom() + win->resizeIncrements().height() - 1, true);

        // check that it hasn't grown outside of the area, due to size increments
        auto const area = workspace()->clientArea(
            MovementArea,
            QPoint(win->frameGeometry().center().x(), (win->y() + newbottom) / 2),
            win->desktop());
        if (area.bottom() >= newbottom) {
            geom.setBottom(newbottom);
        }
    }

    geom.setSize(adjusted_size(win, geom.size(), size_mode::fixed_height));
    workspace()->updateFocusMousePosition(Cursor::pos()); // may cause leave event;
    win->setFrameGeometry(geom);
}

template<typename Win>
void shrink_vertical(Win* win)
{
    if (!win->isResizable() || win->isShade()) {
        return;
    }

    auto geom = win->frameGeometry();
    geom.setBottom(workspace()->packPositionUp(win, geom.bottom(), false));
    if (geom.height() <= 1) {
        return;
    }

    geom.setSize(adjusted_size(win, geom.size(), size_mode::fixed_height));
    if (geom.height() > 20) {
        workspace()->updateFocusMousePosition(Cursor::pos()); // may cause leave event;
        win->setFrameGeometry(geom);
    }
}

}

#endif
