/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "debug/console/model_helpers.h"

#include "win/space_qobject.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

namespace KWin::debug
{

using wayland_space = win::wayland::space<base::wayland::platform>;
using wayland_window = win::wayland::window<wayland_space>;

template<typename Model, typename Space>
void wayland_model_setup_connections(Model& model, Space& space)
{
    for (auto window : space.windows) {
        if (auto wwin = dynamic_cast<wayland_window*>(window); wwin && !wwin->remnant) {
            model.m_shellClients.emplace_back(std::make_unique<console_window>(window));
        }
    }

    // TODO: that only includes windows getting shown, not those which are only created
    QObject::connect(space.qobject.get(),
                     &win::space::qobject_t::wayland_window_added,
                     &model,
                     [&model](auto win) {
                         add_window(&model, model.s_waylandClientId - 1, model.m_shellClients, win);
                     });
    QObject::connect(space.qobject.get(),
                     &win::space::qobject_t::wayland_window_removed,
                     &model,
                     [&model](auto win) {
                         remove_window(
                             &model, model.s_waylandClientId - 1, model.m_shellClients, win);
                     });
}

}
