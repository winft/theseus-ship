/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/geo.h"

#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Win>
bool accepts_input(Win* win, QPoint const& pos)
{
    if (!win->surface) {
        // Only wl_surfaces provide means of limiting the input region. So just accept otherwise.
        return true;
    }
    if (win->surface->state().input_is_infinite) {
        return true;
    }

    auto const input_region = win->surface->state().input;
    auto const local_point = pos - win::frame_to_client_pos(win, win->geo.pos());

    return input_region.contains(local_point);
}

}
