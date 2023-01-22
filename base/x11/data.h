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
    xcb_screen_t* screen{nullptr};
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

inline xcb_screen_t* get_default_screen(x11::data const& data)
{
    if (data.screen) {
        return data.screen;
    }

    int screen = data.screen_number;

    for (auto it = xcb_setup_roots_iterator(xcb_get_setup(data.connection)); it.rem;
         --screen, xcb_screen_next(&it)) {
        if (screen == 0) {
            const_cast<x11::data&>(data).screen = it.data;
        }
    }
    return data.screen;
}

}
