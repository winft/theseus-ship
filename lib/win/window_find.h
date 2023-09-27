/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "desktop_get.h"
#include "meta.h"

namespace KWin::win
{

template<typename Space>
auto find_desktop(Space* space, bool topmost, int desktop)
    -> std::optional<typename Space::window_t>
{
    // TODO(fsorr): use C++20 std::ranges::reverse_view
    auto const& list = space->stacking.order.stack;
    auto is_desktop = [desktop](auto window) {
        return std::visit(overload{[desktop](auto&& window) {
                              return window->control && on_desktop(*window, desktop)
                                  && win::is_desktop(window) && window->isShown();
                          }},
                          window);
    };

    if (topmost) {
        auto it = std::find_if(list.rbegin(), list.rend(), is_desktop);
        if (it != list.rend()) {
            return *it;
        }
    } else {
        // bottom-most
        auto it = std::find_if(list.begin(), list.end(), is_desktop);
        if (it != list.end()) {
            return *it;
        }
    }

    return {};
}

}
