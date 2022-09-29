/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/window.h"

namespace KWin::win::x11
{

struct xcb_windows {
    // Most outer window that encompasses all other windows.
    base::x11::xcb::window outer{};

    // Window with the same dimensions as client.
    // TODO(romangg): Why do we need this again?
    base::x11::xcb::window wrapper{};

    // The actual client window.
    base::x11::xcb::window client{};

    // Including decoration.
    base::x11::xcb::window input{};

    // For move-resize operations.
    base::x11::xcb::window grab{};
};

}
