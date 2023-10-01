/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <utils/algorithm.h>

#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Space>
auto space_windows_find(Space const& space, Wrapland::Server::Surface const* surface) ->
    typename Space::wayland_window*
{
    using window = typename Space::wayland_window;

    if (!surface) {
        // TODO(romangg): assert instead?
        return nullptr;
    }

    auto it = std::find_if(space.windows.cbegin(), space.windows.cend(), [surface](auto win) {
        return std::visit(overload{[&](window* win) { return win->surface == surface; },
                                   [&](auto&& /*win*/) { return false; }},
                          win);
    });
    return it != space.windows.cend() ? std::get<window*>(*it) : nullptr;
}

}
