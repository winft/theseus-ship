/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <xcb/xcb.h>

namespace KWin::base::x11
{

struct data {
    int screen_number{-1};
    xcb_timestamp_t time{XCB_TIME_CURRENT_TIME};
    xcb_window_t root_window{XCB_WINDOW_NONE};
    xcb_connection_t* connection{nullptr};
};

inline void advance_time(x11::data& data, xcb_timestamp_t time)
{
    if (time > data.time) {
        data.time = time;
    }
}

inline void set_time(x11::data& data, xcb_timestamp_t time)
{
    if (time != 0) {
        data.time = time;
    }
}

}
