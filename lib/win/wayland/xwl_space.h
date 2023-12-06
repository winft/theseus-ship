/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "subsurface.h"
#include "surface.h"
#include "xwl_window.h"
#include <win/wayland/space_setup.h>
#include <win/x11/space_setup.h>

#include "win/input.h"
#include "win/screen.h"
#include "win/setup.h"
#include "win/stacking_order.h"
#include "win/x11/desktop_space.h"
#include "win/x11/space_areas.h"
#include "xwl/surface.h"
#include <debug/console/wayland/xwl_console.h>
#include <win/kill_window.h>
#include <win/stacking_state.h>
#include <win/wayland/internal_window.h>
#include <win/wayland/subspace_manager.h>
#include <win/x11/debug.h>
#include <win/x11/netinfo_helpers.h>

#include <memory>

namespace KWin::win::wayland
{

struct xwl_space_mod {
};

template<typename Base, typename Mod = xwl_space_mod>
class xwl_space
{
public:
    using type = xwl_space<Base, Mod>;
    using qobject_t = space_qobject;
    using base_t = Base;
    using input_t = typename base_t::input_t::redirect_t;
    using x11_window = xwl_window<type>;
    using wayland_window = wayland::window<type>;
    using internal_window_t = internal_window<type>;
    using window_t = std::variant<wayland_window*, internal_window_t*, x11_window*>;
    using window_group_t = x11::group<type>;
    using render_outline_t = typename base_t::render_t::qobject_t::outline_t;

    template<typename Render, typename Input>
    xwl_space(Render& render, Input& input)
        : base{input.base}
    {
        space_setup_init(*this, render, input);

        // For Xwayland windows we need to setup Plasma management too.
        QObject::connect(
            qobject.get(), &type::qobject_t::clientAdded, qobject.get(), [this](auto win_id) {
                auto win = windows_map.at(win_id);
                space_setup_handle_x11_window_added(*this, std::get<x11_window*>(win));
            });

        init_space(*this);
    }

    virtual ~xwl_space()
    {
        space_setup_clear(*this);
    }

    void resize(QSize const& size)
    {
        // TODO(romangg): Only call with Xwayland compiled.
        x11::handle_desktop_resize(root_info.get(), size);
        handle_desktop_resize(*this, size);
    }

    void handle_subspace_changed(uint subspace)
    {
        // TODO(romangg): Only call with Xwayland compiled.
        x11::popagate_subspace_change(*this, subspace);
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
                                        space_areas& areas)
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

    void show_debug_console()
    {
        auto console = new debug::xwl_console(*this);
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

    void debug(QString& support) const
    {
        x11::debug_support_info(*this, support);
    }

    base_t& base;

    std::unique_ptr<qobject_t> qobject;
    std::unique_ptr<win::options> options;

    win::space_areas areas;
    std::unique_ptr<base::x11::atoms> atoms;
    std::unique_ptr<QQmlEngine> qml_engine;
    std::unique_ptr<rules::book> rule_book;

    std::unique_ptr<base::x11::event_filter> m_wasUserInteractionFilter;
    std::unique_ptr<base::x11::event_filter> m_movingClientFilter;
    std::unique_ptr<base::x11::event_filter> m_syncAlarmFilter;

    int initial_subspace{1};
    std::unique_ptr<base::x11::xcb::window> m_nullFocus;

    int block_focus{0};

    QPoint focusMousePos;

    // Timer to collect requests for 'reconfigure'
    QTimer reconfigureTimer;
    QTimer updateToolWindowsTimer;

    // Array of the previous restricted areas that window cannot be moved into
    std::vector<win::strut_rects> oldrestrictedmovearea;

    std::unique_ptr<wayland::subspace_manager> subspace_manager;
    std::unique_ptr<x11::session_manager> session_manager;

    QTimer* m_quickTileCombineTimer{nullptr};
    win::quicktiles m_lastTilingMode{win::quicktiles::none};

    QWidget* active_popup{nullptr};

    std::vector<x11::session_info*> session;

    // Delay(ed) window focus timer and client
    QTimer* delayFocusTimer{nullptr};

    bool showing_desktop{false};
    bool was_user_interaction{false};

    int session_active_client;
    int session_desktop;

    win::shortcut_dialog* client_keys_dialog{nullptr};
    bool global_shortcuts_disabled{false};

    // array of previous sizes of xinerama screens
    std::vector<QRect> oldscreensizes;

    // previous sizes od displayWidth()/displayHeight()
    QSize olddisplaysize;

    int set_active_client_recursion{0};

    base::x11::xcb::window shape_helper_window;

    uint32_t window_id{0};

    std::unique_ptr<render_outline_t> outline;
    std::unique_ptr<edger_t> edges;
    std::unique_ptr<deco::bridge<type>> deco;
    std::unique_ptr<dbus::appmenu> appmenu;
    std::unique_ptr<x11::root_info<type>> root_info;

    std::unique_ptr<input_t> input;
    std::unordered_map<std::string, xcb_cursor_t> xcb_cursors;

    std::unique_ptr<win::tabbox<type>> tabbox;
    std::unique_ptr<osd_notification<input_t>> osd;
    std::unique_ptr<kill_window<type>> window_killer;
    std::unique_ptr<win::user_actions_menu<type>> user_actions_menu;

    Mod mod;

    std::unique_ptr<Wrapland::Server::Compositor> compositor;
    std::unique_ptr<Wrapland::Server::Subcompositor> subcompositor;
    std::unique_ptr<Wrapland::Server::XdgShell> xdg_shell;
    std::unique_ptr<Wrapland::Server::LayerShellV1> layer_shell;

    std::unique_ptr<Wrapland::Server::XdgDecorationManager> xdg_decoration_manager;
    std::unique_ptr<Wrapland::Server::XdgForeign> xdg_foreign;

    std::unique_ptr<Wrapland::Server::plasma_activation_feedback> plasma_activation_feedback;
    std::unique_ptr<Wrapland::Server::PlasmaShell> plasma_shell;
    std::unique_ptr<Wrapland::Server::PlasmaWindowManager> plasma_window_manager;
    std::unique_ptr<Wrapland::Server::PlasmaVirtualDesktopManager> plasma_subspace_manager;

    std::unique_ptr<Wrapland::Server::IdleInhibitManagerV1> idle_inhibit_manager_v1;

    std::unique_ptr<Wrapland::Server::AppmenuManager> appmenu_manager;
    std::unique_ptr<Wrapland::Server::ServerSideDecorationPaletteManager>
        server_side_decoration_palette_manager;

    std::unique_ptr<wayland::xdg_activation<type>> xdg_activation;

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
