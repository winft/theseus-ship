/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>
Copyright (C) 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "idle_inhibition.h"
#include "workspace.h"

#include "win/wayland/window.h"

#include <Wrapland/Server/kde_idle.h>
#include <Wrapland/Server/surface.h>

#include <algorithm>
#include <functional>

using Wrapland::Server::Surface;

namespace KWin
{

IdleInhibition::IdleInhibition(KdeIdle *idle)
    : QObject(idle)
    , m_idle(idle)
{
    // Workspace is created after the wayland server is initialized.
    connect(kwinApp(), &Application::startup_finished, this, &IdleInhibition::slotWorkspaceCreated);
}

IdleInhibition::~IdleInhibition() = default;

void IdleInhibition::register_window(win::wayland::window* window)
{
    auto updateInhibit = [this, window] {
        update(window);
    };

    if (!window->control) {
        // Only Wayland windows with explicit control are allowed to inhibit idle for now.
        return;
    }

    m_connections[window] = connect(window->surface(), &Surface::inhibitsIdleChanged, this, updateInhibit);
    connect(window, &win::wayland::window::desktopChanged, this, updateInhibit);
    connect(window, &win::wayland::window::clientMinimized, this, updateInhibit);
    connect(window, &win::wayland::window::clientUnminimized, this, updateInhibit);
    connect(window, &win::wayland::window::windowHidden, this, updateInhibit);
    connect(window, &win::wayland::window::windowShown, this, updateInhibit);
    connect(window, &win::wayland::window::windowClosed, this,
        [this, window] {
            uninhibit(window);
            auto it = m_connections.find(window);
            if (it != m_connections.end()) {
                disconnect(it.value());
                m_connections.erase(it);
            }
        }
    );

    updateInhibit();
}

void IdleInhibition::inhibit(Toplevel* window)
{
    if (isInhibited(window)) {
        // already inhibited
        return;
    }
    m_idleInhibitors << window;
    m_idle->inhibit();
    // TODO: notify powerdevil?
}

void IdleInhibition::uninhibit(Toplevel* window)
{
    auto it = std::find(m_idleInhibitors.begin(), m_idleInhibitors.end(), window);
    if (it == m_idleInhibitors.end()) {
        // not inhibited
        return;
    }
    m_idleInhibitors.erase(it);
    m_idle->uninhibit();
}

void IdleInhibition::update(Toplevel* window)
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

void IdleInhibition::slotWorkspaceCreated()
{
    connect(workspace(), &Workspace::currentDesktopChanged, this, &IdleInhibition::slotDesktopChanged);
}

void IdleInhibition::slotDesktopChanged()
{
    workspace()->forEachAbstractClient([this] (Toplevel* t) { update(t); });
}

}
