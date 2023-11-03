/*
    SPDX-FileCopyrightText: 2022 Francesco Sorrentino <francesco.sorr@gmail.com>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "win/desktop_get.h"
#include <utils/algorithm.h>

#include <QObject>
#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

// Check if @p window inhibits idle.
template<typename Win>
void idle_update(Win& window)
{
    auto const is_visible = window.isShown() && on_current_subspace(window);

    if (is_visible && window.surface && window.surface->inhibitsIdle()) {
        if (!window.inhibit_idle) {
            window.inhibit_idle = true;
            window.space.base.input->idle.inhibit();
        }
    } else {
        if (window.inhibit_idle) {
            window.inhibit_idle = false;
            window.space.base.input->idle.uninhibit();
        }
    }
}

template<typename Space>
void idle_update_all(Space const& space)
{
    for (auto win : space.windows) {
        std::visit(overload{[](typename Space::wayland_window* win) {
                                if (win->control) {
                                    idle_update(*win);
                                }
                            },
                            [](auto&&) {}},
                   win);
    }
}

// Setup @p window's connections to @p idle inhibition, use only for windows with control.
template<typename Win>
void idle_setup(Win& window)
{
    auto update = [&window] { idle_update(window); };
    auto qwin = window.qobject.get();

    QObject::connect(window.surface, &Wrapland::Server::Surface::inhibitsIdleChanged, qwin, update);
    QObject::connect(qwin, &Win::qobject_t::subspaces_changed, qwin, update);
    QObject::connect(qwin, &Win::qobject_t::clientMinimized, qwin, update);
    QObject::connect(qwin, &Win::qobject_t::clientUnminimized, qwin, update);
    QObject::connect(qwin, &Win::qobject_t::windowHidden, qwin, update);
    QObject::connect(qwin, &Win::qobject_t::windowShown, qwin, update);
    QObject::connect(qwin, &Win::qobject_t::closed, qwin, [&window] {
        if (window.inhibit_idle) {
            window.inhibit_idle = false;
            window.space.base.input->idle.uninhibit();
        }
    });

    idle_update(window);
}

}
