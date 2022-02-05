/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xcbutils.h"

namespace KWin::win::x11
{

template<typename Win>
Xcb::Property fetch_skip_close_animation(Win&& win)
{
    return Xcb::Property(false,
                         win.xcb_window(),
                         win.space.atoms->kde_skip_close_animation,
                         XCB_ATOM_CARDINAL,
                         0,
                         1);
}

}
