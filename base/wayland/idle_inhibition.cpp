/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "idle_inhibition.h"

#include "main.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Server/kde_idle.h>
#include <Wrapland/Server/surface.h>
#include <algorithm>
#include <functional>

using Wrapland::Server::Surface;

namespace KWin::base::wayland
{

idle_inhibition::idle_inhibition(KdeIdle* idle)
    : QObject(idle)
    , m_idle(idle)
{
    // Workspace is created after the wayland server is initialized.
    connect(
        kwinApp(), &Application::startup_finished, this, &idle_inhibition::slotWorkspaceCreated);
}

idle_inhibition::~idle_inhibition() = default;

void idle_inhibition::register_window(win::wayland::window* window)
{
    auto updateInhibit = [this, window] { update(window); };

    if (!window->control) {
        // Only Wayland windows with explicit control are allowed to inhibit idle for now.
        return;
    }

    m_connections[window]
        = connect(window->surface(), &Surface::inhibitsIdleChanged, this, updateInhibit);
    connect(window, &win::wayland::window::desktopChanged, this, updateInhibit);
    connect(window, &win::wayland::window::clientMinimized, this, updateInhibit);
    connect(window, &win::wayland::window::clientUnminimized, this, updateInhibit);
    connect(window, &win::wayland::window::windowHidden, this, updateInhibit);
    connect(window, &win::wayland::window::windowShown, this, updateInhibit);
    connect(window, &win::wayland::window::closed, this, [this, window] {
        uninhibit(window);
        auto it = m_connections.find(window);
        if (it != m_connections.end()) {
            disconnect(it.value());
            m_connections.erase(it);
        }
    });

    updateInhibit();
}

void idle_inhibition::inhibit(Toplevel* window)
{
    if (isInhibited(window)) {
        // already inhibited
        return;
    }
    m_idleInhibitors << window;
    m_idle->inhibit();
    // TODO: notify powerdevil?
}

void idle_inhibition::uninhibit(Toplevel* window)
{
    auto it = std::find(m_idleInhibitors.begin(), m_idleInhibitors.end(), window);
    if (it == m_idleInhibitors.end()) {
        // not inhibited
        return;
    }
    m_idleInhibitors.erase(it);
    m_idle->uninhibit();
}

void idle_inhibition::update(Toplevel* window)
{
    if (window->isInternal()) {
        return;
    }

    if (window->isClient()) {
        // XWayland clients do not support the idle-inhibit protocol (and at worst let it crash
        // in the past because there was no surface yet).
        return;
    }

    // TODO: Don't honor the idle inhibitor object if the shell client is not
    // on the current activity (currently, activities are not supported).
    const bool visible = window->isShown() && window->isOnCurrentDesktop();
    if (visible && window->surface() && window->surface()->inhibitsIdle()) {
        inhibit(window);
    } else {
        uninhibit(window);
    }
}

void idle_inhibition::slotWorkspaceCreated()
{
    connect(workspace(),
            &win::space::currentDesktopChanged,
            this,
            &idle_inhibition::slotDesktopChanged);
}

void idle_inhibition::slotDesktopChanged()
{
    for (auto win : workspace()->m_windows) {
        if (win->control) {
            update(win);
        }
    }
}

}
