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
#include "debug/console/wayland/wayland_console.h"
#include "input/wayland/platform.h"
#include "input/wayland/redirect.h"
#include "scripting/platform.h"
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

#include <Wrapland/Server/appmenu.h>
#include <Wrapland/Server/compositor.h>
#include <Wrapland/Server/idle_inhibit_v1.h>
#include <Wrapland/Server/plasma_activation_feedback.h>
#include <Wrapland/Server/plasma_shell.h>
#include <Wrapland/Server/server_decoration_palette.h>
#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/xdg_shell.h>

#include <memory>

namespace KWin::win::wayland
{

template<typename Base>
class space : public win::space
{
public:
    using base_t = Base;
    using type = space<base_t>;
    using x11_window = xwl_window<type>;
    using wayland_window = wayland::window<type>;
    using internal_window_t = internal_window<type>;
    using window_t = std::variant<wayland_window*, internal_window_t*, x11_window*>;

    using input_t = input::wayland::redirect<typename Base::input_t, type>;

    space(Base& base, base::wayland::server* server)
        : win::space()
        , base{base}
        , outline{render::outline::create(*base.render->compositor,
                                          [this] {
                                              return render::create_outline_visual(
                                                  *this->base.render->compositor, *outline);
                                          })}
        , deco{std::make_unique<deco::bridge<type>>(*this)}
        , appmenu{std::make_unique<dbus::appmenu>(dbus::create_appmenu_callbacks(*this))}
        , user_actions_menu{std::make_unique<win::user_actions_menu<type>>(*this)}
        , server{server}
        , compositor{server->display->createCompositor()}
        , subcompositor{server->display->createSubCompositor()}
        , xdg_shell{server->display->createXdgShell()}
        , layer_shell{server->display->createLayerShellV1()}
        , xdg_decoration_manager{server->display->createXdgDecorationManager(xdg_shell.get())}
        , xdg_foreign{server->display->createXdgForeign()}
        , plasma_activation_feedback{server->display->create_plasma_activation_feedback()}
        , plasma_shell{server->display->createPlasmaShell()}
        , plasma_window_manager{server->display->createPlasmaWindowManager()}
        , plasma_virtual_desktop_manager{server->display->createPlasmaVirtualDesktopManager()}
        , idle_inhibit_manager_v1{server->display->createIdleInhibitManager()}
        , appmenu_manager{server->display->createAppmenuManager()}
        , server_side_decoration_palette_manager{
              server->display->createServerSideDecorationPaletteManager()}
    {
        namespace WS = Wrapland::Server;
        using wayland_window = win::wayland::window<space<Base>>;

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

        input = std::make_unique<input_t>(*base.input, *this);
        this->dbus = std::make_unique<base::dbus::kwin_impl<type>>(*this);
        edges = std::make_unique<edger_t>(*this);

        plasma_window_manager->setShowingDesktopState(
            Wrapland::Server::PlasmaWindowManager::ShowingDesktopState::Disabled);
        plasma_window_manager->setVirtualDesktopManager(plasma_virtual_desktop_manager.get());
        virtual_desktop_manager->setVirtualDesktopManagement(plasma_virtual_desktop_manager.get());

        QObject::connect(stacking.order.qobject.get(),
                         &stacking_order_qobject::render_restack,
                         qobject.get(),
                         [this] {
                             for (auto win : windows) {
                                 std::visit(
                                     overload{[&](internal_window_t* win) {
                                                  if (win->isShown()) {
                                                      stacking.order.render_overlays.push_back(win);
                                                  }
                                              },
                                              [](auto&&) {}},
                                     win);
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

        xdg_activation = std::make_unique<wayland::xdg_activation<space>>(*this);
        QObject::connect(qobject.get(), &space::qobject_t::clientActivated, qobject.get(), [this] {
            if (stacking.active) {
                xdg_activation->clear();
            }
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
        QObject::connect(stacking.order.qobject.get(),
                         &stacking_order_qobject::changed,
                         plasma_window_manager.get(),
                         [this] { plasma_manage_update_stacking_order(*this); });

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

        // For Xwayland windows we need to setup Plasma management too.
        QObject::connect(
            qobject.get(), &space::qobject_t::clientAdded, qobject.get(), [this](auto win_id) {
                auto win = windows_map.at(win_id);
                handle_x11_window_added(std::get<x11_window*>(win));
            });

        QObject::connect(virtual_desktop_manager->qobject.get(),
                         &virtual_desktop_manager_qobject::desktopRemoved,
                         qobject.get(),
                         [this](auto&& desktop) { handle_desktop_removed(desktop); });

        init_space(*this);
    }

    ~space() override
    {
        stacking.order.lock();

        auto const windows_copy = windows;
        for (auto const& win : windows_copy) {
            std::visit(overload{[&](wayland_window* win) {
                                    if (!win->remnant) {
                                        destroy_window(win);
                                        remove_all(windows, window_t(win));
                                    }
                                },
                                [](auto&&) {}},
                       win);
        }

        clear_space(*this);
    }

    void resize(QSize const& size) override
    {
        // TODO(romangg): Only call with Xwayland compiled.
        x11::handle_desktop_resize(root_info.get(), size);
        handle_desktop_resize(*this, size);
    }

    void handle_desktop_changed(uint desktop) override
    {
        // TODO(romangg): Only call with Xwayland compiled.
        x11::popagate_desktop_change(*this, desktop);

        for (auto win : windows) {
            std::visit(overload{[](wayland_window* win) {
                                    if (win->control) {
                                        idle_update(*win);
                                    }
                                },
                                [](auto&&) {}},
                       win);
        }
    }

    /// Internal window means a window created by KWin itself.
    internal_window_t* findInternal(QWindow* window) const
    {
        if (!window) {
            return nullptr;
        }

        for (auto win : windows) {
            if (!std::holds_alternative<internal_window_t*>(win)) {
                continue;
            }

            auto internal = std::get<internal_window_t*>(win);
            if (internal->internalWindow() == window) {
                return internal;
            }
        }

        return nullptr;
    }

    using edger_t = screen_edger<type>;
    std::unique_ptr<screen_edge<edger_t>> create_screen_edge(edger_t& edger)
    {
        return std::make_unique<screen_edge<edger_t>>(&edger);
    }

    template<typename Win>
    QRect get_icon_geometry(Win const* win) const
    {
        auto management = win->control->plasma_wayland_integration;
        if (!management || !waylandServer()) {
            // window management interface is only available if the surface is mapped
            return QRect();
        }

        auto min_distance = INT_MAX;
        wayland_window* candidate_panel{nullptr};
        QRect candidate_geo;

        for (auto i = management->minimizedGeometries().constBegin(),
                  end = management->minimizedGeometries().constEnd();
             i != end;
             ++i) {
            auto client = find_window(i.key());
            if (!client) {
                continue;
            }
            auto const distance = QPoint(client->geo.pos() - win->geo.pos()).manhattanLength();
            if (distance < min_distance) {
                min_distance = distance;
                candidate_panel = client;
                candidate_geo = i.value();
            }
        }
        if (!candidate_panel) {
            return QRect();
        }
        return candidate_geo.translated(candidate_panel->geo.pos());
    }

    wayland_window* find_window(Wrapland::Server::Surface* surface) const
    {
        if (!surface) {
            // TODO(romangg): assert instead?
            return nullptr;
        }

        auto it = std::find_if(windows.cbegin(), windows.cend(), [surface](auto win) {
            return std::visit(overload{[&](wayland_window* win) { return win->surface == surface; },
                                       [&](auto&& /*win*/) { return false; }},
                              win);
        });
        return it != windows.cend() ? std::get<wayland_window*>(*it) : nullptr;
    }

    void handle_window_added(wayland_window* window)
    {
        if (window->control && !window->layer_surface) {
            setup_space_window_connections(this, window);
            window->updateDecoration(false);
            update_layer(window);

            auto const area = space_window_area(
                *this, PlacementArea, get_current_output(window->space), get_desktop(*window));
            auto placementDone = false;

            if (window->isInitialPositionSet()) {
                placementDone = true;
            }
            if (window->control->fullscreen) {
                placementDone = true;
            }
            if (window->maximizeMode() == maximize_mode::full) {
                placementDone = true;
            }
            if (window->control->rules.checkPosition(geo::invalid_point, true)
                != geo::invalid_point) {
                placementDone = true;
            }
            if (!placementDone) {
                window->placeIn(area);
            }
        }

        assert(!contains(stacking.order.pre_stack, window_t(window)));
        stacking.order.pre_stack.push_back(window);
        stacking.order.update_order();

        if (window->control) {
            update_space_areas(*this);

            if (window->wantsInput() && !window->control->minimized) {
                activate_window(*this, *window);
            }

            update_tabbox(*this);

            QObject::connect(window->qobject.get(),
                             &wayland_window::qobject_t::windowShown,
                             qobject.get(),
                             [this, window] {
                                 update_layer(window);
                                 stacking.order.update_count();
                                 update_space_areas(*this);
                                 if (window->wantsInput()) {
                                     activate_window(*this, *window);
                                 }
                             });
            QObject::connect(window->qobject.get(),
                             &wayland_window::qobject_t::windowHidden,
                             qobject.get(),
                             [this] {
                                 // TODO: update tabbox if it's displayed
                                 stacking.order.update_count();
                                 update_space_areas(*this);
                             });

            idle_setup(*window);
        }

        adopt_transient_children(this, window);
        Q_EMIT qobject->wayland_window_added(window->meta.signal_id);
    }
    void handle_window_removed(wayland_window* window)
    {
        remove_all(windows, window_t(window));

        if (window->control) {
            if (window_t(window) == stacking.most_recently_raised) {
                stacking.most_recently_raised = {};
            }
            if (window_t(window) == stacking.delayfocus_window) {
                cancel_delay_focus(*this);
            }
            if (window_t(window) == stacking.last_active) {
                stacking.last_active = {};
            }
            if (window_t(window) == client_keys_client) {
                setup_window_shortcut_done(*this, false);
            }
            if (!window->control->shortcut.isEmpty()) {
                // Remove from client_keys.
                set_shortcut(window, QString());
            }
            process_window_hidden(*this, *window);
            Q_EMIT qobject->clientRemoved(window->meta.signal_id);
        }

        stacking.order.update_count();

        if (window->control) {
            update_space_areas(*this);
            update_tabbox(*this);
        }

        Q_EMIT qobject->wayland_window_removed(window->meta.signal_id);
    }

    void update_space_area_from_windows(QRect const& desktop_area,
                                        std::vector<QRect> const& screens_geos,
                                        space_areas& areas) override
    {
        for (auto window : windows) {
            std::visit(overload{[&](x11_window* win) {
                                    if (win->control) {
                                        x11::update_space_areas(
                                            win, desktop_area, screens_geos, areas);
                                    }
                                },
                                [&](auto&&) {}},
                       window);
        }

        // TODO(romangg): Combine this and above loop.
        for (auto win : windows) {
            std::visit(overload{[&](wayland_window* win) {
                                    // TODO(romangg): check on control like in the previous loop?
                                    update_space_areas(win, desktop_area, screens_geos, areas);
                                },
                                [&](auto&&) {}},
                       win);
        }
    }

    void show_debug_console() override
    {
        auto console = new debug::wayland_console(*this);
        console->show();
    }

    Base& base;

    std::unique_ptr<scripting::platform<type>> scripting;
    std::unique_ptr<render::outline> outline;
    std::unique_ptr<edger_t> edges;
    std::unique_ptr<deco::bridge<type>> deco;
    std::unique_ptr<dbus::appmenu> appmenu;

    std::unique_ptr<x11::root_info<type>> root_info;
    std::unique_ptr<x11::color_mapper<type>> color_mapper;

    std::unique_ptr<input_t> input;

    std::unique_ptr<win::tabbox<type>> tabbox;
    std::unique_ptr<osd_notification<input_t>> osd;
    std::unique_ptr<kill_window<type>> window_killer;
    std::unique_ptr<win::user_actions_menu<type>> user_actions_menu;
    std::unique_ptr<base::dbus::kwin_impl<type>> dbus;

    base::wayland::server* server;

    std::unique_ptr<Wrapland::Server::Compositor> compositor;
    std::unique_ptr<Wrapland::Server::Subcompositor> subcompositor;
    std::unique_ptr<Wrapland::Server::XdgShell> xdg_shell;
    std::unique_ptr<Wrapland::Server::LayerShellV1> layer_shell;

    std::unique_ptr<Wrapland::Server::XdgDecorationManager> xdg_decoration_manager;
    std::unique_ptr<Wrapland::Server::XdgForeign> xdg_foreign;

    std::unique_ptr<Wrapland::Server::plasma_activation_feedback> plasma_activation_feedback;
    std::unique_ptr<Wrapland::Server::PlasmaShell> plasma_shell;
    std::unique_ptr<Wrapland::Server::PlasmaWindowManager> plasma_window_manager;
    std::unique_ptr<Wrapland::Server::PlasmaVirtualDesktopManager> plasma_virtual_desktop_manager;

    std::unique_ptr<Wrapland::Server::IdleInhibitManagerV1> idle_inhibit_manager_v1;

    std::unique_ptr<Wrapland::Server::AppmenuManager> appmenu_manager;
    std::unique_ptr<Wrapland::Server::ServerSideDecorationPaletteManager>
        server_side_decoration_palette_manager;

    std::unique_ptr<wayland::xdg_activation<space>> xdg_activation;

    QVector<Wrapland::Server::PlasmaShellSurface*> plasma_shell_surfaces;

    std::vector<window_t> windows;
    std::unordered_map<uint32_t, window_t> windows_map;
    std::vector<win::x11::group<type>*> groups;

    stacking_state<window_t> stacking;

    std::optional<window_t> active_popup_client;
    std::optional<window_t> client_keys_client;
    std::optional<window_t> move_resize_window;

private:
    void handle_x11_window_added(x11_window* window)
    {
        auto setup_plasma_management_for_x11 = [this, window] {
            setup_plasma_management(this, window);

            // X11 windows can be added to the stacking order before they are ready to be painted.
            // The stacking order changed update comes too early because of that. As a workaround
            // update the stacking order explicitly one more time here.
            // TODO(romangg): Can we add an X11 window late to the stacking order, i.e. once it's
            //                ready to be painted? This way we would not need this additional call.
            plasma_manage_update_stacking_order(*this);
        };

        if (window->render_data.ready_for_painting) {
            setup_plasma_management_for_x11();
        } else {
            QObject::connect(
                window->qobject.get(),
                &x11_window::qobject_t::windowShown,
                qobject.get(),
                [setup_plasma_management_for_x11] { setup_plasma_management_for_x11(); });
        }
    }

    void handle_desktop_removed(virtual_desktop* desktop)
    {
        for (auto const& win : windows) {
            std::visit(overload{[this, desktop](auto&& win) {
                           if (!win->control || !win->topo.desktops.contains(desktop)) {
                               return;
                           }

                           if (win->topo.desktops.count() > 1) {
                               leave_desktop(win, desktop);
                               return;
                           }
                           send_window_to_desktop(
                               *this,
                               win,
                               qMin(desktop->x11DesktopNumber(), virtual_desktop_manager->count()),
                               true);
                       }},
                       win);
        }
    }
};

}
