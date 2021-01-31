/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/fullscreen.h"

namespace KWin::win
{

template<>
void fullscreen_restore_geometry(wayland::window* win)
{
    assert(!has_special_geometry_mode_besides_fullscreen(win));

    // In case the restore geometry is invalid, use the placement from the rectify function.
    auto restore_geo = rectify_fullscreen_restore_geometry(win);

    if (!win->restore_geometries.maximize.isValid()) {
        // We let the client decide on a size.
        restore_geo.setSize(QSize(0, 0));
    }

    win->setFrameGeometry(restore_geo);
    win->restore_geometries.maximize = QRect();
}

}
