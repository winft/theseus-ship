/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "move.h"
#include "net.h"
#include "placement.h"
#include "types.h"

namespace KWin::win
{

template<typename Win>
void update_no_border(Win* win)
{
    // Only if maximized windows are without borders a change might be implied.
    if (win->space.options->qobject->borderlessMaximizedWindows()) {
        win->handle_update_no_border();
    }
}

template<typename Win>
void set_restore_geometry(Win* win, QRect const& restore_geo)
{
    if (win->geo.update.fullscreen) {
        // We keep the restore geometry for later fullscreen restoration.
        return;
    }
    if (win->control->quicktiling != quicktiles::none) {
        // We keep the restore geometry for later quicktile restoration.
        return;
    }
    if (is_move(win)) {
        // We keep the restore geometry from the move.
        return;
    }

    win->geo.restore.max = restore_geo;
}

template<typename Win>
QRect get_maximizing_area(Win* win)
{
    QRect area;

    if (win->control->electric_maximizing) {
        area = space_window_area(
            win->space, MaximizeArea, win->space.input->cursor->pos(), get_desktop(*win));
    } else {
        area = space_window_area(win->space, MaximizeArea, win);
    }

    return area;
}

template<typename Win>
QRect rectify_restore_geometry(Win* win, QRect restore_geo)
{
    if (restore_geo.isValid()) {
        return restore_geo;
    }

    auto area = get_maximizing_area(win);

    auto frame_size = QSize(area.width() * 2 / 3, area.height() * 2 / 3);
    if (restore_geo.width() > 0) {
        frame_size.setWidth(restore_geo.width());
    }
    if (restore_geo.height() > 0) {
        frame_size.setHeight(restore_geo.height());
    }

    geometry_updates_blocker blocker(win);
    auto const old_frame_geo = win->geo.update.frame;

    // We need to do a temporary placement to find the right coordinates.
    win->setFrameGeometry(QRect(QPoint(), frame_size));
    win::place_smart(win, area);

    // Get the geometry and reset back to original geometry.
    restore_geo = win->geo.update.frame;
    win->setFrameGeometry(old_frame_geo);

    if (restore_geo.width() > 0) {
        restore_geo.moveLeft(restore_geo.x());
    }
    if (restore_geo.height() > 0) {
        restore_geo.moveTop(restore_geo.y());
    }

    return restore_geo;
}

template<typename Win>
void maximize_restore(Win* win)
{
    auto const old_mode = win->geo.update.max_mode;
    auto const restore_geo = win->geo.restore.max;
    auto final_restore_geo = win->geo.update.frame;

    if (flags(old_mode & maximize_mode::vertical)) {
        final_restore_geo.setTop(restore_geo.top());
        final_restore_geo.setBottom(restore_geo.bottom());
    }
    if (flags(old_mode & maximize_mode::horizontal)) {
        final_restore_geo.setLeft(restore_geo.left());
        final_restore_geo.setRight(restore_geo.right());
    }

    geometry_updates_blocker blocker(win);
    win->apply_restore_geometry(final_restore_geo);

    if constexpr (requires(Win win) { win.net_info; }) {
        win->net_info->setState(x11::net::States(), x11::net::Max);
    }

    win->geo.update.max_mode = maximize_mode::restore;
    update_no_border(win);
    set_restore_geometry(win, QRect());
}

template<typename Win>
void maximize_vertically(Win* win)
{
    auto& geo_update = win->geo.update;
    auto const old_frame_geo = geo_update.frame;
    auto const area = get_maximizing_area(win);

    auto pos = QPoint(old_frame_geo.x(), area.top());
    pos = win->control->rules.checkPosition(pos);

    auto size = QSize(old_frame_geo.width(), area.height());
    size = win->control->adjusted_frame_size(size, size_mode::fixed_height);

    geometry_updates_blocker blocker(win);
    win->setFrameGeometry(QRect(pos, size));

    if constexpr (requires(Win win) { win.net_info; }) {
        auto net_state = flags(geo_update.max_mode & maximize_mode::horizontal) ? x11::net::Max
                                                                                : x11::net::MaxVert;
        win->net_info->setState(net_state, x11::net::Max);
    }

    geo_update.max_mode |= maximize_mode::vertical;
    update_no_border(win);
    set_restore_geometry(win, old_frame_geo);
}

template<typename Win>
void maximize_horizontally(Win* win)
{
    auto& geo_update = win->geo.update;
    auto const old_frame_geo = geo_update.frame;
    auto const area = get_maximizing_area(win);

    auto pos = QPoint(area.left(), old_frame_geo.y());
    pos = win->control->rules.checkPosition(pos);

    auto size = QSize(area.width(), old_frame_geo.height());
    size = win->control->adjusted_frame_size(size, size_mode::fixed_width);

    geometry_updates_blocker blocker(win);
    win->setFrameGeometry(QRect(pos, size));

    if constexpr (requires(Win win) { win.net_info; }) {
        auto net_state = flags(geo_update.max_mode & maximize_mode::vertical) ? x11::net::Max
                                                                              : x11::net::MaxHoriz;
        win->net_info->setState(net_state, x11::net::Max);
    }

    geo_update.max_mode |= maximize_mode::horizontal;
    update_no_border(win);
    set_restore_geometry(win, old_frame_geo);
}

template<typename Win>
void update_maximized_impl(Win* win, maximize_mode mode)
{
    assert(win->geo.update.max_mode != mode);

    if (mode == maximize_mode::restore) {
        maximize_restore(win);
        return;
    }

    auto const old_frame_geo = win->geo.update.frame;
    auto const old_mode = win->geo.update.max_mode;

    if (flags(mode & maximize_mode::vertical)) {
        if (flags(old_mode & maximize_mode::horizontal) && !(mode & maximize_mode::horizontal)) {
            // We switch from horizontal or full maximization to vertical maximization.
            // Restore first to get the right horizontal position.
            maximize_restore(win);
        }
        maximize_vertically(win);
    }
    if (flags(mode & maximize_mode::horizontal)) {
        if (flags(old_mode & maximize_mode::vertical) && !(mode & maximize_mode::vertical)) {
            // We switch from vertical or full maximization to horizontal maximization.
            // Restore first to get the right vertical position.
            maximize_restore(win);
        }
        maximize_horizontally(win);
    }

    set_restore_geometry(win, old_frame_geo);
}

template<typename Win>
void update_maximized(Win* win, maximize_mode mode)
{
    if (!win->isResizable() || is_toolbar(win)) {
        return;
    }

    mode = win->control->rules.checkMaximize(mode);

    geometry_updates_blocker blocker(win);
    auto const old_mode = win->geo.update.max_mode;

    if (mode == old_mode) {
        // Just update the current size.
        auto const restore_geo = win->geo.restore.max;
        if (flags(mode & maximize_mode::vertical)) {
            maximize_vertically(win);
        }
        if (flags(mode & maximize_mode::horizontal)) {
            maximize_horizontally(win);
        }
        set_restore_geometry(win, restore_geo);
        return;
    }

    if (old_mode != maximize_mode::restore && mode != maximize_mode::restore) {
        // We switch between different (partial) maximization modes. First restore the previous one.
        // The call will reset the restore geometry. So undo this change.
        auto const restore_geo = win->geo.restore.max;
        update_maximized_impl(win, maximize_mode::restore);
        win->geo.restore.max = restore_geo;
    }

    update_maximized_impl(win, mode);

    // TODO(romangg): This quicktiling logic is ill-fitted in update_maximized(..). We need to
    //                rework the relation between quicktiling and maximization in general.
    auto old_quicktiling = win->control->quicktiling;
    if (mode == maximize_mode::full) {
        win->control->quicktiling = quicktiles::maximize;
    } else {
        win->control->quicktiling = quicktiles::none;
    }
    if (old_quicktiling != win->control->quicktiling) {
        // Send changed signal but ensure we do not override our frame geometry.
        auto const frame_geo = win->geo.update.frame;
        Q_EMIT win->qobject->quicktiling_changed();
        win->setFrameGeometry(frame_geo);
    }
}

}
