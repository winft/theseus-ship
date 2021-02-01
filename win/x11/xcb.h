/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "atoms.h"
#include "xcbutils.h"

namespace KWin::win::x11
{

inline Xcb::Property fetch_skip_close_animation(xcb_window_t window)
{
    return Xcb::Property(false, window, atoms->kde_skip_close_animation, XCB_ATOM_CARDINAL, 0, 1);
}

}
