/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/logging.h"
#include "base/types.h"

#include <QtGui/private/qtx11extras_p.h>
#include <cerrno>
#include <unistd.h>
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

template<typename Base>
void update_time_from_clock(Base& base)
{
    auto get_monotonic_time = []() -> uint32_t {
        timespec ts;

        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            qCWarning(KWIN_CORE, "Failed to query monotonic time: %s", strerror(errno));
        }

        return ts.tv_sec * 1000 + ts.tv_nsec / 1000000L;
    };

    switch (base.operation_mode) {
    case operation_mode::x11:
        set_time(base.x11_data, QX11Info::getTimestamp());
        break;

    case operation_mode::xwayland:
        set_time(base.x11_data, get_monotonic_time());
        break;

    default:
        // Do not update the current X11 time stamp if it's the Wayland only session.
        break;
    }
}

}
