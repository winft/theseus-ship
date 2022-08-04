/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "meta.h"
#include "move.h"
#include "placement.h"
#include "types.h"

namespace KWin::win
{

template<typename Win>
bool has_special_geometry_mode_besides_fullscreen(Win* win)
{
    return win->geometry_update.max_mode != maximize_mode::restore
        || win->control->quicktiling() != quicktiles::none || is_move(win);
}

template<typename Win>
QRect rectify_fullscreen_restore_geometry(Win* win)
{
    if (win->restore_geometries.maximize.isValid()) {
        return win->restore_geometries.maximize;
    }

    auto const client_area = space_window_area(win->space, PlacementArea, win);
    auto const frame_size = win->control->adjusted_frame_size(client_area.size() * 2 / 3.,
                                                              win::size_mode::fixed_height);

    // Placement requires changes to the current frame geometry.
    auto const old_frame_geo = win->geometry_update.frame;
    win->setFrameGeometry(QRect(QPoint(), frame_size));
    win::place_smart(win, client_area);

    auto const rectified_frame_geo = win->geometry_update.frame;
    win->setFrameGeometry(old_frame_geo);

    return rectified_frame_geo;
}

template<typename Win>
void fullscreen_restore_special_mode(Win* win)
{
    assert(has_special_geometry_mode_besides_fullscreen(win));

    // Window is still in some special geometry mode and we need to adapt the geometry for that.
    if (win->geometry_update.max_mode != maximize_mode::restore) {
        win->update_maximized(win->geometry_update.max_mode);
    } else if (win->control->quicktiling() != quicktiles::none) {
        auto const old_quicktiling = win->control->quicktiling();
        auto const old_restore_geo = win->restore_geometries.maximize;
        set_quicktile_mode(win, quicktiles::none, false);
        set_quicktile_mode(win, old_quicktiling, false);
        win->restore_geometries.maximize = old_restore_geo;
    } else {
        assert(is_move(win));
        // TODO(romangg): Is this case relevant?
    }
}

template<typename Win>
void update_fullscreen_enable(Win* win)
{
    if (!win->restore_geometries.maximize.isValid()) {
        win->restore_geometries.maximize = win->geometry_update.frame;
    }
    win->setFrameGeometry(space_window_area(win->space, FullScreenArea, win));
}

template<typename Win>
void update_fullscreen_disable(Win* win)
{
    auto const old_output = win->central_output;

    if (has_special_geometry_mode_besides_fullscreen(win)) {
        fullscreen_restore_special_mode(win);
    } else {
        win->restore_geometry_from_fullscreen();
    }

    if (old_output && old_output != win->central_output) {
        send_to_screen(win->space, win, *old_output);
    }
}

template<typename Win>
void update_fullscreen(Win* win, bool full, bool user)
{
    full = win->control->rules().checkFullScreen(full);

    auto const was_fullscreen = win->geometry_update.fullscreen;

    if (was_fullscreen == full) {
        return;
    }

    if (is_special_window(win)) {
        return;
    }
    if (user && !win->userCanSetFullScreen()) {
        return;
    }

    geometry_updates_blocker blocker(win);
    win->geometry_update.fullscreen = full;

    end_move_resize(win);
    win->updateDecoration(false, false);
    win->handle_update_fullscreen(full);
}

}
