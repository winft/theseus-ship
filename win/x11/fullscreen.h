/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"

#include "win/fullscreen.h"

namespace KWin::win::x11
{

template<typename Win>
void propagate_fullscreen_update(Win* win, bool full)
{
    if (full) {
        win->info->setState(NET::FullScreen, NET::FullScreen);
        update_fullscreen_enable(win);
        if (win->info->fullscreenMonitors().isSet()) {
            win->setFrameGeometry(fullscreen_monitors_area(win, win->info->fullscreenMonitors()));
        }
    } else {
        win->info->setState(NET::States(), NET::FullScreen);
        update_fullscreen_disable(win);
    }
}

template<typename Win>
bool user_can_set_fullscreen(Win const& win)
{
    if (!win.control->can_fullscreen()) {
        return false;
    }
    return is_normal(&win) || is_dialog(&win);
}

template<typename Win>
void restore_geometry_from_fullscreen(Win& win)
{
    assert(!has_special_geometry_mode_besides_fullscreen(&win));
    win.setFrameGeometry(rectify_fullscreen_restore_geometry(&win));
    win.geo.restore.max = {};
}

}
