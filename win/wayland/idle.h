/*
    SPDX-FileCopyrightText: 2022 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include <QObject>
#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

// Check if @p window inhibits idle.
template<typename Device>
void idle_update(Device& idle, win::wayland::window& window)
{
    auto const is_visible = window.isShown() && window.isOnCurrentDesktop();

    if (is_visible && window.surface && window.surface->inhibitsIdle()) {
        if (!window.inhibit_idle) {
            window.inhibit_idle = true;
            idle.inhibit();
        }
    } else {
        if (window.inhibit_idle) {
            window.inhibit_idle = false;
            idle.uninhibit();
        }
    }
}

// Setup @p window's connections to @p idle inhibition;
// use only for windows with control.
template<typename Device>
void idle_setup(Device& idle, win::wayland::window& window)
{
    auto update = [&idle, &window] { idle_update(idle, window); };

    QObject::connect(
        window.surface, &Wrapland::Server::Surface::inhibitsIdleChanged, &window, update);
    QObject::connect(&window, &win::wayland::window::desktopChanged, &window, update);
    QObject::connect(&window, &win::wayland::window::clientMinimized, &window, update);
    QObject::connect(&window, &win::wayland::window::clientUnminimized, &window, update);
    QObject::connect(&window, &win::wayland::window::windowHidden, &window, update);
    QObject::connect(&window, &win::wayland::window::windowShown, &window, update);
    QObject::connect(&window, &win::wayland::window::closed, &window, [&idle, &window](auto) {
        if (window.inhibit_idle) {
            window.inhibit_idle = false;
            idle.uninhibit();
        }
    });

    idle_update(idle, window);
}

}
