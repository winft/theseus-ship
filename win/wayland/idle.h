/*
    SPDX-FileCopyrightText: 2022 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <QObject>
#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

// Check if @p window inhibits idle.
template<typename Device, typename Win>
void idle_update(Device& idle, Win& window)
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
template<typename Device, typename Win>
void idle_setup(Device& idle, Win& window)
{
    auto update = [&idle, &window] { idle_update(idle, window); };

    QObject::connect(
        window.surface, &Wrapland::Server::Surface::inhibitsIdleChanged, &window, update);
    QObject::connect(&window, &Win::desktopChanged, &window, update);
    QObject::connect(&window, &Win::clientMinimized, &window, update);
    QObject::connect(&window, &Win::clientUnminimized, &window, update);
    QObject::connect(&window, &Win::windowHidden, &window, update);
    QObject::connect(&window, &Win::windowShown, &window, update);
    QObject::connect(&window, &Win::closed, &window, [&idle, &window](auto) {
        if (window.inhibit_idle) {
            window.inhibit_idle = false;
            idle.uninhibit();
        }
    });

    idle_update(idle, window);
}

}
