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
    auto const& mov_res = win->control->move_resize();
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

template<typename Win>
QRect client_to_frame_rect(Win win, QRect const& content_rect)
{
    auto frame = content_rect;

    frame += frame_margins(win);
    frame -= win->client_frame_extents;

    return frame;
}

template<typename Win>
QPoint client_to_frame_pos(Win win, QPoint const& content_pos)
{
    return client_to_frame_rect(win, QRect(content_pos, QSize())).topLeft();
}

template<typename Win>
QSize client_to_frame_size(Win win, QSize const& content_size)
{
    return client_to_frame_rect(win, QRect(QPoint(), content_size)).size();
}

template<typename Win>
QRect frame_to_client_rect(Win win, QRect const& frame_rect)
{
    auto content = frame_rect;

    content -= frame_margins(win);
    content += win->client_frame_extents;

    return content;
}

template<typename Win>
QPoint frame_to_client_pos(Win win, QPoint const& frame_pos)
{
    return frame_to_client_rect(win, QRect(frame_pos, QSize())).topLeft();
}

template<typename Win>
QSize frame_to_client_size(Win win, QSize const& frame_size)
{
    return frame_to_client_rect(win, QRect(QPoint(), frame_size)).size();
}

template<typename Win>
QRect frame_relative_client_rect(Win* win)
{
    auto const frame_geo = win->frameGeometry();
    auto const client_geo = frame_to_client_rect(win, frame_geo);

    return client_geo.translated(-frame_geo.topLeft());
}

template<typename Win>
QRect frame_to_render_rect(Win win, QRect const& frame_rect)
{
    auto content = frame_rect;

    if (!win->has_in_content_deco) {
        content -= frame_margins(win);
    }

    content += win->client_frame_extents;

    return content;
}

template<typename Win>
QPoint frame_to_render_pos(Win win, QPoint const& frame_pos)
{
    return frame_to_render_rect(win, QRect(frame_pos, QSize())).topLeft();
}

template<typename Win>
QRect render_geometry(Win* win)
{
    return frame_to_render_rect(win, win->frameGeometry());
}

template<typename Win>
QSize frame_size(Win* win)
{
    return QSize(left_border(win) + right_border(win), top_border(win) + bottom_border(win));
}

/**
 * Geometry of @param win that accepts input. Can be larger than frame to support resizing outside
 * of the window.
 */
template<typename Win>
QRect input_geometry(Win* win)
{
    if (auto deco = decoration(win)) {
        return win->frameGeometry() + deco->resizeOnlyBorders();
    }

    return win->bufferGeometry() | win->frameGeometry();
}

/**
 * Geometry of content. Relative to the position of @param win.
 */
template<typename Win>
QRect content_geometry(Win* win)
{
    if (auto remnant = win->remnant()) {
        return remnant->contents_rect;
    }
    return frame_relative_client_rect(win);
}

/**
 * Adjust the frame size @p frame according to the size hints of @p win.
 */
template<typename Win>
QSize adjusted_size(Win* win, QSize const& frame, size_mode mode)
{
    // first, get the window size for the given frame size s
    auto wsize = frame_to_client_size(win, frame);

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
    return win->sizeForClientSize(frame_to_client_size(win, win->size()));
}

template<typename Win>
void grow_horizontal(Win* win)
{
    if (!win->isResizable() || shaded(win)) {
        return;
    }

    auto frame_geo = win->frameGeometry();
    frame_geo.setRight(workspace()->packPositionRight(win, frame_geo.right(), true));
    auto adjsize = adjusted_size(win, frame_geo.size(), size_mode::fixed_width);

    if (win->frameGeometry().size() == adjsize && frame_geo.size() != adjsize
        && win->resizeIncrements().width() > 1) {
        // Grow by increment.
        auto const grown_right = workspace()->packPositionRight(
            win, frame_geo.right() + win->resizeIncrements().width() - 1, true);

        // Check that it hasn't grown outside of the area, due to size increments.
        // TODO this may be wrong?
        auto const area = workspace()->clientArea(
            MovementArea,
            QPoint((win->pos().x() + grown_right) / 2, win->frameGeometry().center().y()),
            win->desktop());
        if (area.right() >= grown_right) {
            frame_geo.setRight(grown_right);
        }
    }

    frame_geo.setSize(adjusted_size(win, frame_geo.size(), size_mode::fixed_width));
    frame_geo.setSize(adjusted_size(win, frame_geo.size(), size_mode::fixed_height));

    // May cause leave event.
    workspace()->updateFocusMousePosition(Cursor::pos());
    win->setFrameGeometry(frame_geo);
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

    // TODO(romangg): Magic number 20. Why?
    if (geom.width() > 20) {
        // May cause leave event.
        workspace()->updateFocusMousePosition(Cursor::pos());
        win->setFrameGeometry(geom);
    }
}

template<typename Win>
void grow_vertical(Win* win)
{
    if (!win->isResizable() || shaded(win)) {
        return;
    }

    auto frame_geo = win->frameGeometry();
    frame_geo.setBottom(workspace()->packPositionDown(win, frame_geo.bottom(), true));
    auto adjsize = adjusted_size(win, frame_geo.size(), size_mode::fixed_height);

    if (win->frameGeometry().size() == adjsize && frame_geo.size() != adjsize
        && win->resizeIncrements().height() > 1) {
        // Grow by increment.
        auto const newbottom = workspace()->packPositionDown(
            win, frame_geo.bottom() + win->resizeIncrements().height() - 1, true);

        // check that it hasn't grown outside of the area, due to size increments
        auto const area = workspace()->clientArea(
            MovementArea,
            QPoint(win->frameGeometry().center().x(), (win->pos().y() + newbottom) / 2),
            win->desktop());
        if (area.bottom() >= newbottom) {
            frame_geo.setBottom(newbottom);
        }
    }

    frame_geo.setSize(adjusted_size(win, frame_geo.size(), size_mode::fixed_height));

    // May cause leave event.
    workspace()->updateFocusMousePosition(Cursor::pos());
    win->setFrameGeometry(frame_geo);
}

template<typename Win>
void shrink_vertical(Win* win)
{
    if (!win->isResizable() || shaded(win)) {
        return;
    }

    auto frame_geo = win->frameGeometry();
    frame_geo.setBottom(workspace()->packPositionUp(win, frame_geo.bottom(), false));
    if (frame_geo.height() <= 1) {
        return;
    }

    frame_geo.setSize(adjusted_size(win, frame_geo.size(), size_mode::fixed_height));

    if (frame_geo.height() > 20) {
        // May cause leave event.
        workspace()->updateFocusMousePosition(Cursor::pos());
        win->setFrameGeometry(frame_geo);
    }
}

template<typename Win>
void block_geometry_updates(Win* win, bool block)
{
    if (block) {
        if (!win->geometry_update.block) {
            win->geometry_update.pending = pending_geometry::none;
        }
        win->geometry_update.block++;
    } else {
        win->geometry_update.block--;
        if (!win->geometry_update.block && win->geometry_update.pending != pending_geometry::none) {
            if (shaded(win)) {
                win->setFrameGeometry(QRect(win->pos(), adjusted_size(win)),
                                      win::force_geometry::no);
            } else {
                win->setFrameGeometry(win->frameGeometry(), win::force_geometry::no);
            }
            win->geometry_update.pending = pending_geometry::none;
        }
    }
}

template<typename Win>
QRect electric_border_maximize_geometry(Win const* win, QPoint pos, int desktop)
{
    if (win->control->electric() == win::quicktiles::maximize) {
        if (win->maximizeMode() == maximize_mode::full) {
            return win->restore_geometries.maximize;
        } else {
            return workspace()->clientArea(MaximizeArea, pos, desktop);
        }
    }

    auto ret = workspace()->clientArea(MaximizeArea, pos, desktop);

    if (flags(win->control->electric() & win::quicktiles::left)) {
        ret.setRight(ret.left() + ret.width() / 2 - 1);
    } else if (flags(win->control->electric() & win::quicktiles::right)) {
        ret.setLeft(ret.right() - (ret.width() - ret.width() / 2) + 1);
    }

    if (flags(win->control->electric() & win::quicktiles::top)) {
        ret.setBottom(ret.top() + ret.height() / 2 - 1);
    } else if (flags(win->control->electric() & win::quicktiles::bottom)) {
        ret.setTop(ret.bottom() - (ret.height() - ret.height() / 2) + 1);
    }

    return ret;
}

template<typename Win>
void set_electric_maximizing(Win* win, bool maximizing)
{
    win->control->set_electric_maximizing(maximizing);

    if (maximizing) {
        outline()->show(electric_border_maximize_geometry(win, Cursor::pos(), win->desktop()),
                        win->control->move_resize().geometry);
    } else {
        outline()->hide();
    }

    elevate(win, maximizing);
}

template<typename Win>
void delayed_electric_maximize(Win* win)
{
    auto timer = win->control->electric_maximizing_timer();
    if (!timer) {
        timer = new QTimer(win);
        timer->setInterval(250);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [win]() {
            if (is_move(win)) {
                set_electric_maximizing(win, win->control->electric() != quicktiles::none);
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
    win->control->set_electric(tiles);
}

}

#endif
