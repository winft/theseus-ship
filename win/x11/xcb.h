/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/property.h"

namespace KWin::win::x11
{

template<typename Win>
base::x11::xcb::property fetch_skip_close_animation(Win&& win)
{
    return base::x11::xcb::property(
        false, win.xcb_window, win.space.atoms->kde_skip_close_animation, XCB_ATOM_CARDINAL, 0, 1);
}

}
