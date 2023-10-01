/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "subsurface.h"
#include "surface.h"
#include "xwl_window.h"
#include <win/wayland/space_setup.h>
#include <win/x11/space_setup.h>

#include "debug/console/wayland/wayland_console.h"
#include "desktop/screen_locker_watcher.h"
#include "win/input.h"
#include "win/internal_window.h"
#include "win/screen.h"
#include "win/setup.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/virtual_desktops.h"
#include "win/x11/desktop_space.h"
#include "win/x11/space_areas.h"
#include "xwl/surface.h"
#include <win/x11/netinfo_helpers.h>

#include <memory>

namespace KWin::win::wayland
{

template<typename Render, typename Input>
class space : public win::space
{
public:
    using type = space<Render, Input>;
    using base_t = typename Input::base_t;
    using input_t = typename Input::redirect_t;
    using x11_window = xwl_window<type>;
    using wayland_window = wayland::window<type>;
    using internal_window_t = internal_window<type>;
    using window_t = std::variant<wayland_window*, internal_window_t*, x11_window*>;
    using window_group_t = x11::group<type>;
    using render_outline_t = typename base_t::render_t::outline_t;

    space(Render& render, Input& input)
        : win::space(input.base.config.main)
        , base{input.base}
        , outline{render_outline_t::create(
              *render.compositor,
              [this] { return outline->create_visual(*this->base.render->compositor); })}
        , deco{std::make_unique<deco::bridge<type>>(*this)}
        , appmenu{std::make_unique<dbus::appmenu>(dbus::create_appmenu_callbacks(*this))}
        , user_actions_menu{std::make_unique<win::user_actions_menu<type>>(*this)}
        , screen_locker_watcher{std::make_unique<desktop::screen_locker_watcher>()}
        , compositor{std::make_unique<Wrapland::Server::Compositor>(base.server->display.get())}
        , subcompositor{std::make_unique<Wrapland::Server::Subcompositor>(
              base.server->display.get())}
        , xdg_shell{std::make_unique<Wrapland::Server::XdgShell>(base.server->display.get())}
        , layer_shell{std::make_unique<Wrapland::Server::LayerShellV1>(base.server->display.get())}
        , xdg_decoration_manager{std::make_unique<Wrapland::Server::XdgDecorationManager>(
              base.server->display.get(),
              xdg_shell.get())}
        , xdg_foreign{std::make_unique<Wrapland::Server::XdgForeign>(base.server->display.get())}
        , plasma_activation_feedback{std::make_unique<Wrapland::Server::plasma_activation_feedback>(
              base.server->display.get())}
        , plasma_shell{std::make_unique<Wrapland::Server::PlasmaShell>(base.server->display.get())}
        , plasma_window_manager{std::make_unique<Wrapland::Server::PlasmaWindowManager>(
              base.server->display.get())}
        , plasma_virtual_desktop_manager{std::make_unique<
              Wrapland::Server::PlasmaVirtualDesktopManager>(base.server->display.get())}
        , idle_inhibit_manager_v1{std::make_unique<Wrapland::Server::IdleInhibitManagerV1>(
              base.server->display.get())}
        , appmenu_manager{std::make_unique<Wrapland::Server::AppmenuManager>(
              base.server->display.get())}
        , server_side_decoration_palette_manager{
              std::make_unique<Wrapland::Server::ServerSideDecorationPaletteManager>(
                  base.server->display.get())}
    {
        space_setup_init(*this, input);
        init_space(*this);
    }

    ~space() override
    {
        space_setup_clear(*this);
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
        idle_update_all(*this);
    }

    internal_window_t* findInternal(QWindow* window) const
    {
        return space_windows_find_internal(*this, window);
    }

    using edger_t = screen_edger<type>;
    std::unique_ptr<screen_edge<edger_t>> create_screen_edge(edger_t& edger)
    {
        return std::make_unique<screen_edge<edger_t>>(&edger);
    }

    template<typename Win>
    QRect get_icon_geometry(Win const* win) const
    {
        if constexpr (std::is_same_v<Win, wayland_window>) {
            return get_icon_geometry_for_panel(*win);
        }

        return {};
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

    void update_work_area() const
    {
        x11::update_work_areas(*this);
    }

    void update_tool_windows_visibility(bool also_hide)
    {
        x11::update_tool_windows_visibility(this, also_hide);
    }

    template<typename Win>
    void set_active_window(Win& window)
    {
        if (root_info) {
            x11::root_info_set_active_window(*root_info, window);
        }
    }

    void unset_active_window()
    {
        if (root_info) {
            x11::root_info_unset_active_window(*root_info);
        }
    }

    base_t& base;

    std::unique_ptr<render_outline_t> outline;
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

    std::unique_ptr<desktop::screen_locker_watcher> screen_locker_watcher;
    std::unique_ptr<desktop::kde::kwin_impl<type>> dbus;

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
};

}
