/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "space.h"

#include "appmenu.h"
#include "deco.h"
#include "layer_shell.h"
#include "plasma_shell.h"
#include "plasma_window.h"
#include "setup.h"
#include "space_areas.h"
#include "subsurface.h"
#include "surface.h"
#include "transient.h"
#include "window.h"
#include "xdg_activation.h"
#include "xdg_shell.h"

#include "base/wayland/idle_inhibition.h"
#include "screens.h"
#include "wayland_server.h"
#include "win/input.h"
#include "win/screen.h"
#include "win/setup.h"
#include "win/stacking_order.h"
#include "win/virtual_desktops.h"
#include "win/x11/space_areas.h"
#include "win/x11/stacking_tree.h"
#include "xwl/surface.h"

#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

namespace KWin::win::wayland
{

space::space(base::wayland::server* server)
    : server{server}
{
    namespace WS = Wrapland::Server;

    edges = std::make_unique<win::screen_edger>(*this);

    QObject::connect(server->globals->compositor.get(),
                     &WS::Compositor::surfaceCreated,
                     this,
                     [this](auto surface) { xwl::handle_new_surface(this, surface); });

    QObject::connect(server->globals->xdg_shell.get(),
                     &WS::XdgShell::toplevelCreated,
                     this,
                     [this](auto toplevel) { handle_new_toplevel<window>(this, toplevel); });
    QObject::connect(server->globals->xdg_shell.get(),
                     &WS::XdgShell::popupCreated,
                     this,
                     [this](auto popup) { handle_new_popup<window>(this, popup); });

    QObject::connect(server->globals->xdg_decoration_manager.get(),
                     &WS::XdgDecorationManager::decorationCreated,
                     this,
                     [this](auto deco) { handle_new_xdg_deco(this, deco); });

    QObject::connect(server->globals->plasma_shell.get(),
                     &WS::PlasmaShell::surfaceCreated,
                     [this](auto surface) { handle_new_plasma_shell_surface(this, surface); });

    auto idle_inhibition = new base::wayland::idle_inhibition(server->globals->kde_idle.get());
    QObject::connect(
        this, &space::wayland_window_added, idle_inhibition, [idle_inhibition](auto window) {
            idle_inhibition->register_window(static_cast<win::wayland::window*>(window));
        });

    QObject::connect(server->globals->appmenu_manager.get(),
                     &WS::AppmenuManager::appmenuCreated,
                     [this](auto appmenu) { handle_new_appmenu(this, appmenu); });

    QObject::connect(server->globals->server_side_decoration_palette_manager.get(),
                     &WS::ServerSideDecorationPaletteManager::paletteCreated,
                     [this](auto palette) { handle_new_palette(this, palette); });

    QObject::connect(server->globals->plasma_window_manager.get(),
                     &WS::PlasmaWindowManager::requestChangeShowingDesktop,
                     this,
                     [this](auto state) { handle_change_showing_desktop(this, state); });

    QObject::connect(server->globals->subcompositor.get(),
                     &Wrapland::Server::Subcompositor::subsurfaceCreated,
                     this,
                     [this](auto subsurface) { handle_new_subsurface<window>(this, subsurface); });
    QObject::connect(
        server->globals->layer_shell_v1.get(),
        &Wrapland::Server::LayerShellV1::surface_created,
        this,
        [this](auto layer_surface) { handle_new_layer_surface<window>(this, layer_surface); });

    activation.reset(new win::wayland::xdg_activation);
    QObject::connect(this, &Workspace::clientActivated, this, [this] {
        if (activeClient()) {
            activation->clear();
        }
    });

    // For Xwayland windows we need to setup Plasma management too.
    QObject::connect(this, &Workspace::clientAdded, this, &space::handle_x11_window_added);

    QObject::connect(virtual_desktop_manager::self(),
                     &virtual_desktop_manager::desktopRemoved,
                     this,
                     &space::handle_desktop_removed);
}

space::~space()
{
    stacking_order->lock();

    for (auto const& window : m_windows) {
        if (auto win = qobject_cast<win::wayland::window*>(window)) {
            destroy_window(win);
            remove_all(m_windows, win);
        }
    }
}

window* space::find_window(Wrapland::Server::Surface* surface) const
{
    if (!surface) {
        // TODO(romangg): assert instead?
        return nullptr;
    }

    auto it = std::find_if(m_windows.cbegin(), m_windows.cend(), [surface](auto win) {
        return win->surface() == surface;
    });
    return it != m_windows.cend() ? qobject_cast<window*>(*it) : nullptr;
}

void space::handle_wayland_window_shown(Toplevel* window)
{
    QObject::disconnect(window, &Toplevel::windowShown, this, &space::handle_wayland_window_shown);
    handle_window_added(static_cast<win::wayland::window*>(window));
}

void space::handle_window_added(wayland::window* window)
{
    if (window->control && !window->layer_surface) {
        setup_space_window_connections(this, window);
        window->updateDecoration(false);
        win::update_layer(window);

        auto const area
            = clientArea(PlacementArea, kwinApp()->get_base().screens.current(), window->desktop());
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

    adopt_transient_children(this, window);
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

    Q_EMIT wayland_window_removed(window);
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

void space::handle_desktop_removed(virtual_desktop* desktop)
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
                qMin(desktop->x11DesktopNumber(), virtual_desktop_manager::self()->count()),
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
        auto client = find_window(i.key());
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

    // TODO(romangg): Combine this and above loop.
    for (auto win : m_windows) {
        if (auto wl_win = qobject_cast<win::wayland::window*>(win)) {
            update_space_areas(wl_win, desktop_area, screens_geos, areas);
        }
    }
}

}
