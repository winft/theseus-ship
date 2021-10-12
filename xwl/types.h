/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xwayland_interface.h"

#include <xcb/xcb.h>

namespace KWin::Xwl
{

struct x11_data {
    xcb_connection_t* connection{nullptr};
    xcb_screen_t* screen{nullptr};
};

}
