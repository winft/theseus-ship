/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"
#include "desktop_get.h"
#include "net.h"
#include "types.h"

#include <QMargins>
#include <QRect>

namespace KWin::win
{

template<typename Win1, typename Win2>
static inline bool is_irrelevant(Win1 const* window, Win2 const* regarding, int desktop)
{
    if (!window) {
        return true;
    }
    if (!window->control) {
        return true;
    }
    if (window == regarding) {
        return true;
    }
    if (!window->isShown()) {
        return true;
    }
    if (!on_desktop(window, desktop)) {
        return true;
    }
    if (is_desktop(window)) {
        return true;
    }
    return false;
}

/**
 * Returns @c true if @p win is being interactively moved; otherwise @c false.
 */
template<typename Win>
bool is_move(Win* win)
{
    auto const& mov_res = win->control->move_resize;
    return mov_res.enabled && mov_res.contact == position::center;
}

/**
 * Returns margins of server-side decoration with zero margins when no server-side decoration
 * is available for @param win.
 */
template<typename Win>
QMargins frame_margins(Win* win)
{
    if (win->remnant) {
        return win->remnant->data.frame_margins;
    }
    return QMargins(left_border(win), top_border(win), right_border(win), bottom_border(win));
}

template<typename Win>
QRect client_to_frame_rect(Win win, QRect const& content_rect)
{
    auto frame = content_rect;

    frame += frame_margins(win);
    frame -= win->geo.client_frame_extents;

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
    content += win->geo.client_frame_extents;

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
    auto const frame_geo = win->geo.frame;
    auto const client_geo = frame_to_client_rect(win, frame_geo);

    return client_geo.translated(-frame_geo.topLeft());
}

template<typename Win>
QRect frame_to_render_rect(Win win, QRect const& frame_rect)
{
    auto content = frame_rect;

    if (!win->geo.has_in_content_deco) {
        content -= frame_margins(win);
    }

    content += win->geo.client_frame_extents;

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
    return frame_to_render_rect(win, win->geo.frame);
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
        return win->geo.frame + deco->resizeOnlyBorders();
    }

    return frame_to_client_rect(win, win->geo.frame);
}

template<typename Win>
QRect pending_frame_geometry(Win* win)
{
    return win->geo.update.pending == pending_geometry::none ? win->geo.frame
                                                             : win->geo.update.frame;
}

/**
 * Adjust the frame size @p frame according to the size hints of @p win.
 */
template<typename Win>
QSize adjusted_frame_size(Win* win, QSize const& frame_size, size_mode mode)
{
    assert(win->control);
    return win->control->adjusted_frame_size(frame_size, mode);
}

}
