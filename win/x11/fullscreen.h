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

}
