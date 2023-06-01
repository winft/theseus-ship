/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Info>
void root_info_unset_active_window(Info& info)
{
    if (info.m_activeWindow == XCB_WINDOW_NONE) {
        return;
    }
    info.m_activeWindow = XCB_WINDOW_NONE;
    info.setActiveWindow(XCB_WINDOW_NONE);
}

template<typename Info, typename Win>
void root_info_set_active_window(Info& info, Win& window)
{
    if (info.m_activeWindow == window.xcb_windows.client) {
        return;
    }
    info.m_activeWindow = window.xcb_windows.client;
    info.setActiveWindow(window.xcb_windows.client);
}

}
