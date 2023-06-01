/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <algorithm>

namespace KWin::win::wayland
{

template<typename Window, typename Space>
void adopt_transient_children(Space* space, Window* window)
{
    auto const& wins = space->windows;
    std::for_each(wins.cbegin(), wins.cend(), [&window](auto win) {
        // Children can only be of same window type.
        std::visit(overload{[window](Window* win) { win->checkTransient(window); }, [&](auto&&) {}},
                   win);
    });
}

}
