/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "space.h"

#include "appmenu.h"
#include "deco.h"
#include "idle.h"
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

#include "base/wayland/server.h"
#include "win/input.h"
#include "win/internal_window.h"
#include "win/screen.h"
#include "win/setup.h"
#include "win/stacking_order.h"
#include "win/virtual_desktops.h"
#include "win/x11/space_areas.h"
#include "xwl/surface.h"

#if KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

#include <Wrapland/Server/appmenu.h>
#include <Wrapland/Server/compositor.h>
#include <Wrapland/Server/idle_inhibit_v1.h>
#include <Wrapland/Server/kde_idle.h>
#include <Wrapland/Server/plasma_shell.h>
#include <Wrapland/Server/server_decoration_palette.h>
#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/xdg_activation_v1.h>
#include <Wrapland/Server/xdg_shell.h>

namespace KWin::win::wayland
{

space::space(render::compositor& render, base::wayland::server* server)
    : win::space(render)
    , server{server}
    , compositor{server->display->createCompositor()}
    , subcompositor{server->display->createSubCompositor()}
    , xdg_shell{server->display->createXdgShell()}
    , layer_shell{server->display->createLayerShellV1()}
    , xdg_decoration_manager{server->display->createXdgDecorationManager(xdg_shell.get())}
    , xdg_activation{server->display->createXdgActivationV1()}
    , xdg_foreign{server->display->createXdgForeign()}
    , plasma_shell{server->display->createPlasmaShell()}
    , plasma_window_manager{server->display->createPlasmaWindowManager()}
    , plasma_virtual_desktop_manager{server->display->createPlasmaVirtualDesktopManager()}
    , kde_idle{server->display->createIdle()}
    , idle_inhibit_manager_v1{server->display->createIdleInhibitManager()}
    , appmenu_manager{server->display->createAppmenuManager()}
    , server_side_decoration_palette_manager{
          server->display->createServerSideDecorationPaletteManager()}
{
    namespace WS = Wrapland::Server;

    edges = std::make_unique<win::screen_edger>(*this);

    plasma_window_manager->setShowingDesktopState(
        Wrapland::Server::PlasmaWindowManager::ShowingDesktopState::Disabled);
    plasma_window_manager->setVirtualDesktopManager(plasma_virtual_desktop_manager.get());
    virtual_desktop_manager->setVirtualDesktopManagement(plasma_virtual_desktop_manager.get());

    QObject::connect(stacking_order.get(), &stacking_order::render_restack, qobject.get(), [this] {
        for (auto win : m_windows) {
            if (auto iwin = qobject_cast<internal_window*>(win); iwin && iwin->isShown()) {
                stacking_order->render_overlays.push_back(iwin);
            }
        }
    });

    QObject::connect(compositor.get(),
                     &WS::Compositor::surfaceCreated,
                     qobject.get(),
                     [this](auto surface) { xwl::handle_new_surface(this, surface); });

    QObject::connect(xdg_shell.get(),
                     &WS::XdgShell::toplevelCreated,
                     qobject.get(),
                     [this](auto toplevel) { handle_new_toplevel<window>(this, toplevel); });
    QObject::connect(xdg_shell.get(),
                     &WS::XdgShell::popupCreated,
                     qobject.get(),
                     [this](auto popup) { handle_new_popup<window>(this, popup); });

    QObject::connect(xdg_decoration_manager.get(),
                     &WS::XdgDecorationManager::decorationCreated,
                     qobject.get(),
                     [this](auto deco) { handle_new_xdg_deco(this, deco); });

    QObject::connect(
        xdg_activation.get(),
        &WS::XdgActivationV1::token_requested,
        qobject.get(),
        [this](auto token) { win::wayland::xdg_activation_handle_token_request(*this, *token); });
    QObject::connect(xdg_activation.get(),
                     &WS::XdgActivationV1::activate,
                     qobject.get(),
                     [this](auto const& token, auto surface) {
                         handle_xdg_activation_activate(this, token, surface);
                     });

    QObject::connect(plasma_shell.get(),
                     &WS::PlasmaShell::surfaceCreated,
                     qobject.get(),
                     [this](auto surface) { handle_new_plasma_shell_surface(this, surface); });

    QObject::connect(
        qobject.get(), &space::qobject_t::currentDesktopChanged, kde_idle.get(), [this] {
            for (auto win : m_windows) {
                if (!win->control) {
                    continue;
                }
                if (auto wlwin = qobject_cast<wayland::window*>(win)) {
                    idle_update(*kde_idle, *wlwin);
                }
            }
        });

    QObject::connect(appmenu_manager.get(),
                     &WS::AppmenuManager::appmenuCreated,
                     [this](auto appmenu) { handle_new_appmenu(this, appmenu); });

    QObject::connect(server_side_decoration_palette_manager.get(),
                     &WS::ServerSideDecorationPaletteManager::paletteCreated,
                     [this](auto palette) { handle_new_palette(this, palette); });

    QObject::connect(plasma_window_manager.get(),
                     &WS::PlasmaWindowManager::requestChangeShowingDesktop,
                     qobject.get(),
                     [this](auto state) { handle_change_showing_desktop(this, state); });
    QObject::connect(qobject.get(),
                     &win::space::qobject_t::showingDesktopChanged,
                     qobject.get(),
                     [this](bool set) {
                         using ShowingState
                             = Wrapland::Server::PlasmaWindowManager::ShowingDesktopState;
                         plasma_window_manager->setShowingDesktopState(
                             set ? ShowingState::Enabled : ShowingState::Disabled);
                     });

    QObject::connect(subcompositor.get(),
                     &WS::Subcompositor::subsurfaceCreated,
                     qobject.get(),
                     [this](auto subsurface) { handle_new_subsurface<window>(this, subsurface); });
    QObject::connect(
        layer_shell.get(),
        &WS::LayerShellV1::surface_created,
        qobject.get(),
        [this](auto layer_surface) { handle_new_layer_surface<window>(this, layer_surface); });

    activation = std::make_unique<wayland::xdg_activation<space>>(*this);
    QObject::connect(
        qobject.get(), &space::qobject_t::clientActivated, qobject.get(), [this](auto&& win) {
            if (win) {
                activation->clear();
            }
        });

    // For Xwayland windows we need to setup Plasma management too.
    QObject::connect(qobject.get(),
                     &space::qobject_t::clientAdded,
                     qobject.get(),
                     [this](auto&& win) { handle_x11_window_added(win); });

    QObject::connect(virtual_desktop_manager.get(),
                     &virtual_desktop_manager::desktopRemoved,
                     qobject.get(),
                     [this](auto&& desktop) { handle_desktop_removed(desktop); });
}

space::~space()
{
    stacking_order->lock();

    for (auto const& window : m_windows) {
        if (auto win = qobject_cast<win::wayland::window*>(window); win && !win->remnant) {
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
        return win->surface == surface;
    });
    return it != m_windows.cend() ? qobject_cast<window*>(*it) : nullptr;
}

void space::handle_window_added(wayland::window* window)
{
    if (window->control && !window->layer_surface) {
        setup_space_window_connections(this, window);
        window->updateDecoration(false);
        win::update_layer(window);

        auto const area
            = clientArea(PlacementArea, get_current_output(window->space), window->desktop());
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
        if (window->control->rules().checkPosition(geo::invalid_point, true)
            != geo::invalid_point) {
            placementDone = true;
        }
        if (!placementDone) {
            window->placeIn(area);
        }
    }

    assert(!contains(stacking_order->pre_stack, window));
    stacking_order->pre_stack.push_back(window);
    stacking_order->update_order();

    if (window->control) {
        updateClientArea();

        if (window->wantsInput() && !window->control->minimized()) {
            activateClient(window);
        }

        updateTabbox();

        QObject::connect(window, &win::wayland::window::windowShown, qobject.get(), [this, window] {
            win::update_layer(window);
            stacking_order->update_count();
            updateClientArea();
            if (window->wantsInput()) {
                activateClient(window);
            }
        });
        QObject::connect(window, &win::wayland::window::windowHidden, qobject.get(), [this] {
            // TODO: update tabbox if it's displayed
            stacking_order->update_count();
            updateClientArea();
        });

        idle_setup(*kde_idle, *window);
    }

    adopt_transient_children(this, window);
    Q_EMIT qobject->wayland_window_added(window);
}

void space::handle_window_removed(wayland::window* window)
{
    remove_all(m_windows, window);

    if (window->control) {
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
        Q_EMIT qobject->clientRemoved(window);
    }

    stacking_order->update_count();

    if (window->control) {
        updateClientArea();
        updateTabbox();
    }

    Q_EMIT qobject->wayland_window_removed(window);
}

void space::handle_x11_window_added(x11::window* window)
{
    if (window->ready_for_painting) {
        setup_plasma_management(this, window);
    } else {
        QObject::connect(window, &x11::window::windowShown, qobject.get(), [this](auto window) {
            setup_plasma_management(this, window);
        });
    }
}

void space::handle_desktop_removed(virtual_desktop* desktop)
{
    for (auto const& client : m_windows) {
        if (!client->control) {
            continue;
        }
        if (!client->desktops().contains(desktop)) {
            continue;
        }

        if (client->desktops().count() > 1) {
            win::leave_desktop(client, desktop);
        } else {
            sendClientToDesktop(
                client, qMin(desktop->x11DesktopNumber(), virtual_desktop_manager->count()), true);
        }
    }
}

Toplevel* space::findInternal(QWindow* window) const
{
    if (!window) {
        return nullptr;
    }

    for (auto win : m_windows) {
        if (auto internal = qobject_cast<internal_window*>(win);
            internal && internal->internalWindow() == window) {
            return internal;
        }
    }

    return nullptr;
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
    for (auto const& window : m_windows) {
        if (!window->control) {
            continue;
        }
        if (auto x11_window = qobject_cast<win::x11::window*>(window)) {
            win::x11::update_space_areas(x11_window, desktop_area, screens_geos, areas);
        }
    }

    // TODO(romangg): Combine this and above loop.
    for (auto win : m_windows) {
        // TODO(romangg): check on control like in the previous loop?
        if (auto wl_win = qobject_cast<win::wayland::window*>(win)) {
            update_space_areas(wl_win, desktop_area, screens_geos, areas);
        }
    }
}

}
