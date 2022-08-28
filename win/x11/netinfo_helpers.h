/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Info, typename Win>
void root_info_set_active_window(Info& info, Win* window)
{
    auto const xcb_win
        = window ? static_cast<xcb_window_t>(window->xcb_window) : xcb_window_t{XCB_WINDOW_NONE};
    if (info.m_activeWindow == xcb_win) {
        return;
    }
    info.m_activeWindow = xcb_win;
    info.setActiveWindow(xcb_win);
}

}
