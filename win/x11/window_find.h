/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <QObject>
#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Win, typename Space>
Win* find_controlled_window(Space& space, predicate_match predicate, xcb_window_t w)
{
    auto find_window = [&](std::function<bool(Win const*)> const& func) -> Win* {
        for (auto win : space.windows) {
            if (!win->control) {
                continue;
            }
            if (auto x11_win = qobject_cast<Win*>(win); x11_win && func(x11_win)) {
                return x11_win;
            }
        }
        return nullptr;
    };

    switch (predicate) {
    case predicate_match::window:
        return find_window([w](auto win) { return win->xcb_window == w; });
    case predicate_match::wrapper_id:
        return find_window([w](auto win) { return win->xcb_windows.wrapper == w; });
    case predicate_match::frame_id:
        return find_window([w](auto win) { return win->xcb_windows.outer == w; });
    case predicate_match::input_id:
        return find_window([w](auto win) { return win->xcb_windows.input == w; });
    }

    return nullptr;
}

}
