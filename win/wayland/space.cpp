/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "space.h"

#include "setup.h"
#include "space_areas.h"
#include "xdg_activation.h"

#include "screens.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "win/input.h"
#include "win/screen.h"
#include "win/setup.h"
#include "win/stacking_order.h"
#include "win/x11/space_areas.h"
#include "win/x11/stacking_tree.h"

#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

namespace KWin::win::wayland
{

space::space()
{
    activation.reset(new win::wayland::xdg_activation);
    QObject::connect(this, &Workspace::clientActivated, this, [this] {
        if (activeClient()) {
            activation->clear();
        }
    });

    QObject::connect(
        waylandServer(), &WaylandServer::window_added, this, &space::handle_window_added);

    QObject::connect(
        waylandServer(), &WaylandServer::window_removed, this, &space::handle_window_removed);

    // For Xwayland windows we need to setup Plasma management too.
    QObject::connect(this, &Workspace::clientAdded, this, &space::handle_x11_window_added);

    QObject::connect(VirtualDesktopManager::self(),
                     &VirtualDesktopManager::desktopRemoved,
                     this,
                     &space::handle_desktop_removed);
}

space::~space()
{
    stacking_order->lock();

    // TODO(romangg): Do we really need both loops?
    auto const windows = waylandServer()->windows;
    for (auto win : windows) {
        win->destroy();
        remove_all(m_windows, win);
    }

    for (auto const& window : m_windows) {
        if (auto win = qobject_cast<win::wayland::window*>(window)) {
            win->destroy();
            remove_all(m_windows, win);
        }
    }
}

void space::handle_window_added(wayland::window* window)
{
    assert(!contains(m_windows, window));

    if (window->control && !window->layer_surface) {
        setup_space_window_connections(this, window);
        window->updateDecoration(false);
        win::update_layer(window);

        auto const area = clientArea(PlacementArea, Screens::self()->current(), window->desktop());
        auto placementDone = false;

        if (window->isInitialPositionSet()) {
            placementDone = true;
        }
        if (window->control->fullscreen()) {
            placementDone = true;
        }
        if (window->maximizeMode() == win::maximize_mode::full) {
            placementDone = true;
        }
        if (window->control->rules().checkPosition(invalidPoint, true) != invalidPoint) {
            placementDone = true;
        }
        if (!placementDone) {
            window->placeIn(area);
        }

        m_allClients.push_back(window);
    }

    m_windows.push_back(window);

    if (!contains(stacking_order->pre_stack, window)) {
        // Raise if it hasn't got any stacking position yet.
        stacking_order->pre_stack.push_back(window);
    }
    if (!contains(stacking_order->sorted(), window)) {
        // It'll be updated later, and updateToolWindows() requires window to be in
        // stacking_order.
        stacking_order->win_stack.push_back(window);
    }

    x_stacking_tree->mark_as_dirty();
    stacking_order->update(true);

    if (window->control) {
        updateClientArea();

        if (window->wantsInput() && !window->control->minimized()) {
            activateClient(window);
        }

        updateTabbox();

        QObject::connect(window, &win::wayland::window::windowShown, this, [this, window] {
            win::update_layer(window);
            x_stacking_tree->mark_as_dirty();
            stacking_order->update(true);
            updateClientArea();
            if (window->wantsInput()) {
                activateClient(window);
            }
        });
        QObject::connect(window, &win::wayland::window::windowHidden, this, [this] {
            // TODO: update tabbox if it's displayed
            x_stacking_tree->mark_as_dirty();
            stacking_order->update(true);
            updateClientArea();
        });
    }
    Q_EMIT wayland_window_added(window);
}

void space::handle_window_removed(wayland::window* window)
{
    remove_all(m_windows, window);

    if (window->control) {
        remove_all(m_allClients, window);
        if (window == most_recently_raised) {
            most_recently_raised = nullptr;
        }
        if (window == delayfocus_client) {
            cancelDelayFocus();
        }
        if (window == last_active_client) {
            last_active_client = nullptr;
        }
        if (window == client_keys_client) {
            setupWindowShortcutDone(false);
        }
        if (!window->control->shortcut().isEmpty()) {
            // Remove from client_keys.
            win::set_shortcut(window, QString());
        }
        clientHidden(window);
        Q_EMIT clientRemoved(window);
    }

    x_stacking_tree->mark_as_dirty();
    stacking_order->update(true);

    if (window->control) {
        updateClientArea();
        updateTabbox();
    }
}

void space::handle_x11_window_added(x11::window* window)
{
    if (window->readyForPainting()) {
        setup_plasma_management(window);
    } else {
        QObject::connect(window, &x11::window::windowShown, this, [](auto window) {
            setup_plasma_management(window);
        });
    }
}

void space::handle_desktop_removed(VirtualDesktop* desktop)
{
    for (auto const& client : m_allClients) {
        if (!client->desktops().contains(desktop)) {
            continue;
        }

        if (client->desktops().count() > 1) {
            win::leave_desktop(client, desktop);
        } else {
            sendClientToDesktop(
                client,
                qMin(desktop->x11DesktopNumber(), VirtualDesktopManager::self()->count()),
                true);
        }
    }
}

QRect space::get_icon_geometry(Toplevel const* win) const
{
    auto management = win->control->wayland_management();
    if (!management || !waylandServer()) {
        // window management interface is only available if the surface is mapped
        return QRect();
    }

    auto min_distance = INT_MAX;
    Toplevel* candidate_panel{nullptr};
    QRect candidate_geo;

    for (auto i = management->minimizedGeometries().constBegin(),
              end = management->minimizedGeometries().constEnd();
         i != end;
         ++i) {
        auto client = waylandServer()->findToplevel(i.key());
        if (!client) {
            continue;
        }
        auto const distance = QPoint(client->pos() - win->pos()).manhattanLength();
        if (distance < min_distance) {
            min_distance = distance;
            candidate_panel = client;
            candidate_geo = i.value();
        }
    }
    if (!candidate_panel) {
        return QRect();
    }
    return candidate_geo.translated(candidate_panel->pos());
}

void space::update_space_area_from_windows(QRect const& desktop_area,
                                           std::vector<QRect> const& screens_geos,
                                           win::space_areas& areas)
{
    for (auto const& window : m_allClients) {
        if (auto x11_window = qobject_cast<win::x11::window*>(window)) {
            win::x11::update_space_areas(x11_window, desktop_area, screens_geos, areas);
        }
    }

    auto const wayland_windows = waylandServer()->windows;
    for (auto win : wayland_windows) {
        update_space_areas(win, desktop_area, screens_geos, areas);
    }
}

}
