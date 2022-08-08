/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "appmenu.h"
#include "deco.h"
#include "idle.h"
#include "kwin_export.h"
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
#include "xwl_window.h"

#include "base/wayland/server.h"
#include "input/wayland/platform.h"
#include "input/wayland/redirect.h"
#include "win/input.h"
#include "win/internal_window.h"
#include "win/screen.h"
#include "win/setup.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/tabbox.h"
#include "win/virtual_desktops.h"
#include "win/x11/desktop_space.h"
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

#include <memory>

namespace KWin::win::wayland
{

template<typename Base>
class space : public win::space
{
public:
    using x11_window = xwl_window<space<Base>>;
    using wayland_window = wayland::window<space<Base>>;

    space(Base& base, base::wayland::server* server)
        : win::space(*base.render->compositor)
        , base{base}
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
        using wayland_window = win::wayland::window<space<Base>>;

        init_space(*this);

        singleton_interface::get_current_output_geometry = [this] {
            auto output = get_current_output(*this);
            return output ? output->geometry() : QRect();
        };
        singleton_interface::set_activation_token
            = [this](auto const& appid) { return xdg_activation_set_token(*this, appid); };
        singleton_interface::create_internal_window = [this](auto qwindow) {
            auto iwin = new win::internal_window(qwindow, *this);
            return iwin->singleton.get();
        };

        this->input = std::make_unique<input::wayland::redirect>(*base.input, *this);
        dbus = std::make_unique<base::dbus::kwin_impl<win::space, input::platform>>(
            *this, base.input.get());
        edges = std::make_unique<screen_edger>(*this);

        plasma_window_manager->setShowingDesktopState(
            Wrapland::Server::PlasmaWindowManager::ShowingDesktopState::Disabled);
        plasma_window_manager->setVirtualDesktopManager(plasma_virtual_desktop_manager.get());
        virtual_desktop_manager->setVirtualDesktopManagement(plasma_virtual_desktop_manager.get());

        QObject::connect(
            stacking_order.get(), &stacking_order::render_restack, qobject.get(), [this] {
                for (auto win : windows) {
                    if (auto iwin = dynamic_cast<internal_window*>(win); iwin && iwin->isShown()) {
                        stacking_order->render_overlays.push_back(iwin);
                    }
                }
            });

        QObject::connect(compositor.get(),
                         &WS::Compositor::surfaceCreated,
                         qobject.get(),
                         [this](auto surface) { xwl::handle_new_surface(this, surface); });

        QObject::connect(
            xdg_shell.get(), &WS::XdgShell::toplevelCreated, qobject.get(), [this](auto toplevel) {
                handle_new_toplevel<wayland_window>(this, toplevel);
            });
        QObject::connect(xdg_shell.get(),
                         &WS::XdgShell::popupCreated,
                         qobject.get(),
                         [this](auto popup) { handle_new_popup<wayland_window>(this, popup); });

        QObject::connect(xdg_decoration_manager.get(),
                         &WS::XdgDecorationManager::decorationCreated,
                         qobject.get(),
                         [this](auto deco) { handle_new_xdg_deco(this, deco); });

        QObject::connect(
            xdg_activation.get(),
            &WS::XdgActivationV1::token_requested,
            qobject.get(),
            [this](auto token) { xdg_activation_handle_token_request(*this, *token); });
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
                         &space::qobject_t::showingDesktopChanged,
                         qobject.get(),
                         [this](bool set) {
                             using ShowingState
                                 = Wrapland::Server::PlasmaWindowManager::ShowingDesktopState;
                             plasma_window_manager->setShowingDesktopState(
                                 set ? ShowingState::Enabled : ShowingState::Disabled);
                         });

        QObject::connect(
            subcompositor.get(),
            &WS::Subcompositor::subsurfaceCreated,
            qobject.get(),
            [this](auto subsurface) { handle_new_subsurface<wayland_window>(this, subsurface); });
        QObject::connect(layer_shell.get(),
                         &WS::LayerShellV1::surface_created,
                         qobject.get(),
                         [this](auto layer_surface) {
                             handle_new_layer_surface<wayland_window>(this, layer_surface);
                         });

        activation = std::make_unique<wayland::xdg_activation<space>>(*this);
        QObject::connect(
            qobject.get(), &space::qobject_t::clientActivated, qobject.get(), [this](auto&& win) {
                if (win) {
                    activation->clear();
                }
            });

        // For Xwayland windows we need to setup Plasma management too.
        QObject::connect(
            qobject.get(), &space::qobject_t::clientAdded, qobject.get(), [this](auto&& win) {
                handle_x11_window_added(static_cast<x11::window*>(win));
            });

        QObject::connect(virtual_desktop_manager->qobject.get(),
                         &virtual_desktop_manager_qobject::desktopRemoved,
                         qobject.get(),
                         [this](auto&& desktop) { handle_desktop_removed(desktop); });
    }

    ~space() override
    {
        stacking_order->lock();

        for (auto const& window : windows) {
            if (auto win = dynamic_cast<wayland_window*>(window); win && !win->remnant) {
                destroy_window(win);
                remove_all(windows, win);
            }
        }

        clear_space(*this);
    }

    void resize(QSize const& size) override
    {
        // TODO(romangg): Only call with Xwayland compiled.
        x11::handle_desktop_resize(size);
        handle_desktop_resize(*this, size);
    }

    void handle_desktop_changed(uint desktop) override
    {
        // TODO(romangg): Only call with Xwayland compiled.
        x11::popagate_desktop_change(*this, desktop);

        for (auto win : windows) {
            if (!win->control) {
                continue;
            }
            if (auto wlwin = dynamic_cast<wayland_window*>(win)) {
                idle_update(*kde_idle, *wlwin);
            }
        }
    }

    Toplevel* findInternal(QWindow* window) const override
    {
        if (!window) {
            return nullptr;
        }

        for (auto win : windows) {
            if (auto internal = dynamic_cast<internal_window*>(win);
                internal && internal->internalWindow() == window) {
                return internal;
            }
        }

        return nullptr;
    }

    QRect get_icon_geometry(Toplevel const* win) const override
    {
        auto management = win->control->plasma_wayland_integration;
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

    wayland_window* find_window(Wrapland::Server::Surface* surface) const
    {
        if (!surface) {
            // TODO(romangg): assert instead?
            return nullptr;
        }

        auto it = std::find_if(windows.cbegin(), windows.cend(), [surface](auto win) {
            return win->surface == surface;
        });
        return it != windows.cend() ? dynamic_cast<wayland_window*>(*it) : nullptr;
    }

    void handle_window_added(wayland_window* window)
    {
        if (window->control && !window->layer_surface) {
            setup_space_window_connections(this, window);
            window->updateDecoration(false);
            update_layer(window);

            auto const area = space_window_area(
                *this, PlacementArea, get_current_output(window->space), window->desktop());
            auto placementDone = false;

            if (window->isInitialPositionSet()) {
                placementDone = true;
            }
            if (window->control->fullscreen()) {
                placementDone = true;
            }
            if (window->maximizeMode() == maximize_mode::full) {
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
            update_space_areas(*this);

            if (window->wantsInput() && !window->control->minimized()) {
                activate_window(*this, window);
            }

            update_tabbox(*this);

            QObject::connect(window->qobject.get(),
                             &wayland_window::qobject_t::windowShown,
                             qobject.get(),
                             [this, window] {
                                 update_layer(window);
                                 stacking_order->update_count();
                                 update_space_areas(*this);
                                 if (window->wantsInput()) {
                                     activate_window(*this, window);
                                 }
                             });
            QObject::connect(window->qobject.get(),
                             &wayland_window::qobject_t::windowHidden,
                             qobject.get(),
                             [this] {
                                 // TODO: update tabbox if it's displayed
                                 stacking_order->update_count();
                                 update_space_areas(*this);
                             });

            idle_setup(*kde_idle, *window);
        }

        adopt_transient_children(this, window);
        Q_EMIT qobject->wayland_window_added(window);
    }
    void handle_window_removed(wayland_window* window)
    {
        remove_all(windows, window);

        if (window->control) {
            if (window == most_recently_raised) {
                most_recently_raised = nullptr;
            }
            if (window == delayfocus_client) {
                cancel_delay_focus(*this);
            }
            if (window == last_active_client) {
                last_active_client = nullptr;
            }
            if (window == client_keys_client) {
                setup_window_shortcut_done(*this, false);
            }
            if (!window->control->shortcut().isEmpty()) {
                // Remove from client_keys.
                set_shortcut(window, QString());
            }
            process_window_hidden(*this, window);
            Q_EMIT qobject->clientRemoved(window);
        }

        stacking_order->update_count();

        if (window->control) {
            update_space_areas(*this);
            update_tabbox(*this);
        }

        Q_EMIT qobject->wayland_window_removed(window);
    }

    void update_space_area_from_windows(QRect const& desktop_area,
                                        std::vector<QRect> const& screens_geos,
                                        space_areas& areas) override
    {
        for (auto const& window : windows) {
            if (!window->control) {
                continue;
            }
            if (auto x11_window = dynamic_cast<x11::window*>(window)) {
                x11::update_space_areas(x11_window, desktop_area, screens_geos, areas);
            }
        }

        // TODO(romangg): Combine this and above loop.
        for (auto win : windows) {
            // TODO(romangg): check on control like in the previous loop?
            if (auto wl_win = dynamic_cast<wayland_window*>(win)) {
                update_space_areas(wl_win, desktop_area, screens_geos, areas);
            }
        }
    }

    Base& base;
    base::wayland::server* server;

    std::unique_ptr<Wrapland::Server::Compositor> compositor;
    std::unique_ptr<Wrapland::Server::Subcompositor> subcompositor;
    std::unique_ptr<Wrapland::Server::XdgShell> xdg_shell;
    std::unique_ptr<Wrapland::Server::LayerShellV1> layer_shell;

    std::unique_ptr<Wrapland::Server::XdgDecorationManager> xdg_decoration_manager;
    std::unique_ptr<Wrapland::Server::XdgActivationV1> xdg_activation;
    std::unique_ptr<Wrapland::Server::XdgForeign> xdg_foreign;

    std::unique_ptr<Wrapland::Server::PlasmaShell> plasma_shell;
    std::unique_ptr<Wrapland::Server::PlasmaWindowManager> plasma_window_manager;
    std::unique_ptr<Wrapland::Server::PlasmaVirtualDesktopManager> plasma_virtual_desktop_manager;

    std::unique_ptr<Wrapland::Server::KdeIdle> kde_idle;
    std::unique_ptr<Wrapland::Server::IdleInhibitManagerV1> idle_inhibit_manager_v1;

    std::unique_ptr<Wrapland::Server::AppmenuManager> appmenu_manager;
    std::unique_ptr<Wrapland::Server::ServerSideDecorationPaletteManager>
        server_side_decoration_palette_manager;

    std::unique_ptr<wayland::xdg_activation<space>> activation;

    QVector<Wrapland::Server::PlasmaShellSurface*> plasma_shell_surfaces;

private:
    void handle_x11_window_added(x11::window* window)
    {
        if (window->ready_for_painting) {
            setup_plasma_management(this, window);
        } else {
            QObject::connect(window->qobject.get(),
                             &x11::window::qobject_t::windowShown,
                             qobject.get(),
                             [this](auto window) { setup_plasma_management(this, window); });
        }
    }

    void handle_desktop_removed(virtual_desktop* desktop)
    {
        for (auto const& client : windows) {
            if (!client->control) {
                continue;
            }
            if (!client->desktops().contains(desktop)) {
                continue;
            }

            if (client->desktops().count() > 1) {
                leave_desktop(client, desktop);
            } else {
                send_window_to_desktop(
                    *this,
                    client,
                    qMin(desktop->x11DesktopNumber(), virtual_desktop_manager->count()),
                    true);
            }
        }
    }
};

}
