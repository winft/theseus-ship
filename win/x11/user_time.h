/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/property.h"
#include "base/x11/xcb/proto.h"
#include "main.h"

#include <NETWM>

namespace KWin::win::x11
{

template<typename Win>
bool has_user_time_support(Win* win)
{
    return win->net_info->userTime() != -1U;
}

/**
 * Updates the user time (time of last action in the active window).
 * This is called inside  kwin for every action with the window
 * that qualifies for user interaction (clicking on it, activate it
 * externally, etc.).
 */
template<typename Win>
void update_user_time(Win* win, xcb_timestamp_t time = XCB_TIME_CURRENT_TIME)
{
    // copied in Group::updateUserTime
    if (time == XCB_TIME_CURRENT_TIME) {
        base::x11::update_time_from_clock(win->space.base);
        time = win->space.base.x11_data.time;
    }
    if (time != -1U
        && (win->user_time == XCB_TIME_CURRENT_TIME
            || NET::timestampCompare(time, win->user_time) > 0)) {
        // time > user_time
        win->user_time = time;
    }

    win->group->updateUserTime(win->user_time);
}

template<typename Win>
xcb_timestamp_t read_user_creation_time(Win& win)
{
    base::x11::xcb::property prop(win.space.base.x11_data.connection,
                                  false,
                                  win.xcb_windows.client,
                                  win.space.atoms->kde_net_wm_user_creation_time,
                                  XCB_ATOM_CARDINAL,
                                  0,
                                  1);
    return prop.value<xcb_timestamp_t>(-1);
}

template<typename Win>
xcb_timestamp_t user_time(Win* win)
{
    auto time = win->user_time;
    if (time == 0) {
        // doesn't want focus after showing
        return 0;
    }

    auto group = win->group;
    assert(group);

    if (time == -1U
        || (group->user_time != -1U && NET::timestampCompare(group->user_time, time) > 0)) {
        time = group->user_time;
    }
    return time;
}

}
