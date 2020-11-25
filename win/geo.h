/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_GEO_H
#define KWIN_WIN_GEO_H

#include "deco.h"
#include "remnant.h"
#include "scene.h"
#include "shadow.h"
#include "types.h"

#include "outline.h"
#include "workspace.h"

#include <QRect>

namespace KWin::win
{

template<typename Win>
void set_shade(Win* win, bool set)
{
    set ? win->setShade(shade::normal) : win->setShade(shade::none);
}

/**
 * Returns @c true if @p win is being interactively moved; otherwise @c false.
 */
template<typename Win>
bool is_move(Win* win)
{
    auto const& mov_res = win->control()->move_resize();
    return mov_res.enabled && mov_res.contact == position::center;
}

/**
 * Returns margins of server-side decoration with zero margins when no server-side decoration
 * is available for @param win.
 */
template<typename Win>
QMargins frame_margins(Win* win)
{
    if (auto remnant = win->remnant()) {
        return remnant->frame_margins;
    }
    return QMargins(left_border(win), top_border(win), right_border(win), bottom_border(win));
}

/**
 * Geometry of @param win that accepts input. Can be larger than frame to support resizing outside
 * of the window.
 */
template<typename Win>
QRect input_geometry(Win* win)
{
    if (auto const& ctrl = win->control()) {
        if (auto const& deco = ctrl->deco(); deco.enabled()) {
            return win->frameGeometry() + deco.decoration->resizeOnlyBorders();
        }
    }

    return win->bufferGeometry() | win->frameGeometry();
}

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

template<typename Win>
QPoint to_client_pos(Win win, QPoint const& pos)
{
    if (auto remnant = win->remnant()) {
        return pos + remnant->contents_rect.topLeft();
    }
    return pos + QPoint(left_border(win), top_border(win));
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
    if (!win->isResizable() || shaded(win)) {
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
            QPoint((win->pos().x() + newright) / 2, win->frameGeometry().center().y()),
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
    if (!win->isResizable() || shaded(win)) {
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
    if (!win->isResizable() || shaded(win)) {
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
            QPoint(win->frameGeometry().center().x(), (win->pos().y() + newbottom) / 2),
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
    if (!win->isResizable() || shaded(win)) {
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
            if (shaded(win)) {
                win->setFrameGeometry(QRect(win->pos(), adjusted_size(win)),
                                      win::force_geometry::no);
            } else {
                win->setFrameGeometry(win->frameGeometry(), win::force_geometry::no);
            }
            ctrl->set_pending_geometry_update(pending_geometry::none);
        }
    }
}

template<typename Win>
QRect electric_border_maximize_geometry(Win const* win, QPoint pos, int desktop)
{
    if (win->control()->electric() == win::quicktiles::maximize) {
        if (win->maximizeMode() == maximize_mode::full) {
            return win->geometryRestore();
        } else {
            return workspace()->clientArea(MaximizeArea, pos, desktop);
        }
    }

    auto ret = workspace()->clientArea(MaximizeArea, pos, desktop);

    if (flags(win->control()->electric() & win::quicktiles::left)) {
        ret.setRight(ret.left() + ret.width() / 2 - 1);
    } else if (flags(win->control()->electric() & win::quicktiles::right)) {
        ret.setLeft(ret.right() - (ret.width() - ret.width() / 2) + 1);
    }

    if (flags(win->control()->electric() & win::quicktiles::top)) {
        ret.setBottom(ret.top() + ret.height() / 2 - 1);
    } else if (flags(win->control()->electric() & win::quicktiles::bottom)) {
        ret.setTop(ret.bottom() - (ret.height() - ret.height() / 2) + 1);
    }

    return ret;
}

/**
 * Window will be temporarily painted as if being at the top of the stack.
 * Only available if Compositor is active, if not active, this method is a no-op.
 */
template<typename Win>
void elevate(Win* win, bool elevate)
{
    if (auto effect_win = win->effectWindow()) {
        effect_win->elevate(elevate);
        win->addWorkspaceRepaint(visible_rect(win));
    }
}

template<typename Win>
void set_electric_maximizing(Win* win, bool maximizing)
{
    win->control()->set_electric_maximizing(maximizing);

    if (maximizing) {
        outline()->show(electric_border_maximize_geometry(win, Cursor::pos(), win->desktop()),
                        win->control()->move_resize().geometry);
    } else {
        outline()->hide();
    }

    elevate(win, maximizing);
}

template<typename Win>
void delayed_electric_maximize(Win* win)
{
    auto timer = win->control()->electric_maximizing_timer();
    if (!timer) {
        timer = new QTimer(win);
        timer->setInterval(250);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [win]() {
            if (is_move(win)) {
                set_electric_maximizing(win, win->control()->electric() != quicktiles::none);
            }
        });
    }
    timer->start();
}

template<typename Win>
void set_electric(Win* win, quicktiles tiles)
{
    if (tiles != quicktiles::maximize) {
        // sanitize the mode, ie. simplify "invalid" combinations
        if ((tiles & quicktiles::horizontal) == quicktiles::horizontal) {
            tiles &= ~quicktiles::horizontal;
        }
        if ((tiles & quicktiles::vertical) == quicktiles::vertical) {
            tiles &= ~quicktiles::vertical;
        }
    }
    win->control()->set_electric(tiles);
}

}

#endif
