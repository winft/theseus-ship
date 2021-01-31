/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/maximize.h"

namespace KWin::win
{

template<>
void update_frame_from_restore_geometry(wayland::window* win, QRect const& restore_geo)
{
    auto rectified_geo = rectify_restore_geometry(win, restore_geo);

    if (!restore_geo.isValid()) {
        // When the restore geometry was not valid we let the client send a new size instead of
        // using the one determined by our rectify function.
        // TODO(romangg): This can offset the relative Placement, e.g. when centered. Place again
        //                later on when we received the new size from client?
        rectified_geo.setSize(QSize());
    }

    win->setFrameGeometry(rectified_geo);
}

}
