/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "debug/console/model_helpers.h"

#include "win/space_qobject.h"

namespace KWin::debug
{

template<typename Model, typename Space>
void wayland_model_setup_connections(Model& model, Space& space)
{
    using wayland_window_t = typename Space::wayland_window;
    using internal_window_t = typename Space::internal_window_t;

    for (auto win : space.windows) {
        std::visit(overload{[&](wayland_window_t* win) {
                                if (!win->remnant) {
                                    model.m_shellClients.emplace_back(
                                        std::make_unique<console_window<wayland_window_t>>(win));
                                }
                            },
                            [](auto&&) {}},
                   win);
    }

    for (auto const& win : space.windows) {
        std::visit(overload{[&](internal_window_t* win) {
                                model.internal_windows.emplace_back(
                                    std::make_unique<console_window<internal_window_t>>(win));
                            },
                            [](auto&&) {}},
                   win);
    }

    // TODO: that only includes windows getting shown, not those which are only created
    QObject::connect(space.qobject.get(),
                     &win::space::qobject_t::wayland_window_added,
                     &model,
                     [&](auto win_id) {
                         auto win = std::get<wayland_window_t*>(space.windows_map.at(win_id));
                         add_window(&model, model.s_waylandClientId - 1, model.m_shellClients, win);
                     });
    QObject::connect(space.qobject.get(),
                     &win::space::qobject_t::wayland_window_removed,
                     &model,
                     [&](auto win_id) {
                         auto win = std::get<wayland_window_t*>(space.windows_map.at(win_id));
                         remove_window(
                             &model, model.s_waylandClientId - 1, model.m_shellClients, win);
                     });
    QObject::connect(
        space.qobject.get(), &win::space_qobject::internalClientAdded, &model, [&](auto win_id) {
            auto win = std::get<internal_window_t*>(space.windows_map.at(win_id));
            add_window(&model, model.s_workspaceInternalId - 1, model.internal_windows, win);
        });
    QObject::connect(
        space.qobject.get(), &win::space_qobject::internalClientRemoved, &model, [&](auto win_id) {
            auto win = std::get<internal_window_t*>(space.windows_map.at(win_id));
            remove_window(&model, model.s_workspaceInternalId - 1, model.internal_windows, win);
        });
}

}
