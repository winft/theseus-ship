/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/window.h"

namespace KWin::win::x11
{

/// Returns all existing screen edge windows.
template<typename Edger>
std::vector<xcb_window_t> screen_edges_windows(Edger const& edger)
{
    std::vector<xcb_window_t> wins;

    for (auto& edge : edger.edges) {
        xcb_window_t w = edge->window_id();
        if (w != XCB_WINDOW_NONE) {
            wins.push_back(w);
        }

        // TODO:  lambda
        w = edge->approachWindow();

        if (w != XCB_WINDOW_NONE) {
            wins.push_back(w);
        }
    }

    return wins;
}

}
