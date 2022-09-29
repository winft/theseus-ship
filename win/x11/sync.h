/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"

#include <QRect>
#include <xcb/sync.h>

namespace KWin::win::x11
{

struct sync_request {
    xcb_sync_counter_t counter{XCB_NONE};
    xcb_sync_alarm_t alarm{XCB_NONE};

    // The update request number is the serial of our latest configure request.
    int64_t update_request_number{0};
    xcb_timestamp_t timestamp{XCB_NONE};

    int suppressed{0};
};

struct configure_event {
    int64_t update_request_number{0};

    // Geometry to apply after a resize operation has been completed.
    struct {
        QRect frame;
        // TODO(romangg): instead of client geometry remember deco and extents margins?
        QRect client;
        maximize_mode max_mode{maximize_mode::restore};
        bool fullscreen{false};
    } geometry;
};

struct synced_geometry {
    QRect frame;
    QRect client;
    maximize_mode max_mode{maximize_mode::restore};
    bool fullscreen{false};
    bool init{true};
};

}
