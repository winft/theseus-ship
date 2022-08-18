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
    for (auto window : space.windows) {
        if (auto wwin = dynamic_cast<typename Space::wayland_window*>(window);
            wwin && !wwin->remnant) {
            model.m_shellClients.emplace_back(
                std::make_unique<console_window<typename Space::window_t>>(window));
        }
    }

    // TODO: that only includes windows getting shown, not those which are only created
    QObject::connect(space.qobject.get(),
                     &win::space::qobject_t::wayland_window_added,
                     &model,
                     [&](auto win_id) {
                         auto win = space.windows_map.at(win_id);
                         add_window(&model, model.s_waylandClientId - 1, model.m_shellClients, win);
                     });
    QObject::connect(space.qobject.get(),
                     &win::space::qobject_t::wayland_window_removed,
                     &model,
                     [&](auto win_id) {
                         auto win = space.windows_map.at(win_id);
                         remove_window(
                             &model, model.s_waylandClientId - 1, model.m_shellClients, win);
                     });
}

}
