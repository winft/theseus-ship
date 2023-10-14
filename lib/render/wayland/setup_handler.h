/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/wayland/setup_window.h>

namespace KWin::render::wayland
{

template<typename Handler>
void effect_setup_handler(Handler& handler)
{
    handler.reconfigure();

    auto space = handler.scene.platform.base.space.get();

    // TODO(romangg): We do this for every window here, even for windows that are not an xdg-shell
    //                type window. Restrict that?
    QObject::connect(space->qobject.get(),
                     &decltype(space->qobject)::element_type::wayland_window_added,
                     &handler,
                     [&handler](auto win_id) {
                         std::visit(
                             overload{[&](auto&& win) { effect_setup_window(handler, *win); }},
                             handler.scene.platform.base.space->windows_map.at(win_id));
                     });

    // TODO(romangg): We do this here too for every window.
    for (auto win : space->windows) {
        using wayland_window = typename Handler::space_t::wayland_window;
        std::visit(overload{[&](wayland_window* win) { effect_setup_window(handler, *win); },
                            [](auto&&) {}},
                   win);
    }
}

}
