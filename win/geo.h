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
 * Adjust the frame size @p frame according to the size hints of @p win.
 */
template<typename Win>
QSize adjusted_size(Win* win, QSize const& frame, size_mode mode)
{
    // first, get the window size for the given frame size s
    auto wsize = win->frameSizeToClientSize(frame);
    if (wsize.isEmpty()) {
        wsize = QSize(qMax(wsize.width(), 1), qMax(wsize.height(), 1));
    }

    return win->sizeForClientSize(wsize, mode, false);
}

/**
 * This helper returns proper size even if the window is shaded,
 * see also the comment in X11Client::setGeometry().
 */
template<typename Win>
QSize adjusted_size(Win* win)
{
    return win->sizeForClientSize(win->clientSize());
}

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

template<typename Win>
void block_geometry_updates(Win* win, bool block)
{
    auto ctrl = win->control();
    if (block) {
        if (!ctrl->geometry_updates_blocked()) {
            ctrl->set_pending_geometry_update(pending_geometry::none);
        }
        ctrl->block_geometry_updates();
    } else {
        ctrl->unblock_geometry_updates();
        if (!ctrl->geometry_updates_blocked()
            && ctrl->pending_geometry_update() != pending_geometry::none) {
            if (win->isShade()) {
                win->setFrameGeometry(QRect(win->pos(), adjusted_size(win)),
                                      win::force_geometry::no);
            } else {
                win->setFrameGeometry(win->frameGeometry(), win::force_geometry::no);
            }
            ctrl->set_pending_geometry_update(pending_geometry::none);
        }
    }
}

}

#endif
