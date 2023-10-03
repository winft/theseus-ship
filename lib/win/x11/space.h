/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "desktop_space.h"
#include "screen_edge.h"
#include "screen_edges.h"
#include "screen_edges_filter.h"
#include "space_areas.h"
#include "space_setup.h"
#include "window.h"

#include "base/x11/xcb/helpers.h"
#include "debug/console/x11/x11_console.h"
#include "desktop/kde/dbus/kwin.h"
#include "desktop/screen_locker_watcher.h"
#include "utils/blocker.h"
#include "win/desktop_space.h"
#include "win/internal_window.h"
#include "win/screen_edges.h"
#include "win/stacking_order.h"
#include <win/space_reconfigure.h>
#include <win/stacking_state.h>
#include <win/user_actions_menu.h>
#include <win/x11/debug.h>
#include <win/x11/netinfo_helpers.h>

#include <vector>

namespace KWin::win::x11
{

template<typename Render, typename Input>
class space
{
public:
    using type = space<Render, Input>;
    using qobject_t = space_qobject;
    using base_t = typename Input::base_t;
    using input_t = typename Input::redirect_t;
    using x11_window = window<type>;
    using window_t = std::variant<x11_window*>;
    using window_group_t = x11::group<type>;
    using render_outline_t = typename base_t::render_t::outline_t;

    space(Render& render, Input& input)
        : base{input.base}
    {
        qobject = std::make_unique<space_qobject>([this] { space_start_reconfigure_timer(*this); });
        options = std::make_unique<win::options>(input.base.config.main);
        rule_book = std::make_unique<rules::book>();
        virtual_desktop_manager = std::make_unique<win::virtual_desktop_manager>();
        session_manager = std::make_unique<win::session_manager>();

        outline = render_outline_t::create(*render.compositor, [this] {
            return outline->create_visual(*this->base.render->compositor);
        });
        deco = std::make_unique<deco::bridge<type>>(*this);
        appmenu = std::make_unique<dbus::appmenu>(dbus::create_appmenu_callbacks(*this));
        user_actions_menu = std::make_unique<win::user_actions_menu<type>>(*this);
        screen_locker_watcher = std::make_unique<desktop::screen_locker_watcher>();

        win::init_space(*this);

        singleton_interface::get_current_output_geometry = [this] {
            auto output = get_current_output(*this);
            return output ? output->geometry() : QRect();
        };

        this->input = input.integrate_space(*this);

        atoms = std::make_unique<base::x11::atoms>(base.x11_data.connection);
        edges = std::make_unique<edger_t>(*this);
        dbus = std::make_unique<desktop::kde::kwin_impl<type>>(*this);

        QObject::connect(
            virtual_desktop_manager->qobject.get(),
            &virtual_desktop_manager_qobject::desktopRemoved,
            qobject.get(),
            [this] {
                auto const desktop_count = static_cast<int>(virtual_desktop_manager->count());
                for (auto const& window : windows) {
                    std::visit(overload{[&](auto&& win) {
                                   if (!win->control) {
                                       return;
                                   }
                                   if (on_all_desktops(*win)) {
                                       return;
                                   }
                                   if (get_desktop(*win) <= desktop_count) {
                                       return;
                                   }
                                   send_window_to_desktop(*this, win, desktop_count, true);
                               }},
                               window);
                }
            });

        x11::init_space(*this);
    }

    virtual ~space()
    {
        x11::clear_space(*this);
        win::clear_space(*this);
    }

    void resize(QSize const& size)
    {
        handle_desktop_resize(root_info.get(), size);
        win::handle_desktop_resize(*this, size);
    }

    void handle_desktop_changed(uint desktop)
    {
        x11::popagate_desktop_change(*this, desktop);
    }

    /// On X11 an internal window is an unmanaged and mapped by the window id.
    x11_window* findInternal(QWindow* window) const
    {
        if (!window) {
            return nullptr;
        }
        return find_unmanaged<x11_window>(*this, window->winId());
    }

    template<typename Win>
    QRect get_icon_geometry(Win const* /*win*/) const
    {
        return {};
    }

    using edger_t = screen_edger<type>;
    std::unique_ptr<win::screen_edge<edger_t>> create_screen_edge(edger_t& edger)
    {
        if (!edges_filter) {
            edges_filter = std::make_unique<screen_edges_filter<type>>(*this);
        }
        return std::make_unique<x11::screen_edge<edger_t>>(&edger, *atoms);
    }

    void update_space_area_from_windows(QRect const& desktop_area,
                                        std::vector<QRect> const& screens_geos,
                                        win::space_areas& areas)
    {
        for (auto const& win : windows) {
            std::visit(overload{[&](x11_window* win) {
                           if (win->control) {
                               update_space_areas(win, desktop_area, screens_geos, areas);
                           }
                       }},
                       win);
        }
    }

    void show_debug_console()
    {
        auto console = new debug::x11_console(*this);
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
    std::unique_ptr<rules::book> rule_book;

    std::unique_ptr<base::x11::event_filter> m_wasUserInteractionFilter;
    std::unique_ptr<base::x11::event_filter> m_movingClientFilter;
    std::unique_ptr<base::x11::event_filter> m_syncAlarmFilter;

    int m_initialDesktop{1};
    std::unique_ptr<base::x11::xcb::window> m_nullFocus;

    int block_focus{0};

    QPoint focusMousePos;

    // Timer to collect requests for 'reconfigure'
    QTimer reconfigureTimer;
    QTimer updateToolWindowsTimer;

    // Array of the previous restricted areas that window cannot be moved into
    std::vector<win::strut_rects> oldrestrictedmovearea;

    std::unique_ptr<win::virtual_desktop_manager> virtual_desktop_manager;
    std::unique_ptr<win::session_manager> session_manager;

    QTimer* m_quickTileCombineTimer{nullptr};
    win::quicktiles m_lastTilingMode{win::quicktiles::none};

    QWidget* active_popup{nullptr};

    std::vector<win::session_info*> session;

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
    std::unique_ptr<x11::root_info<space>> root_info;
    std::unique_ptr<x11::color_mapper<type>> color_mapper;

    std::unique_ptr<input_t> input;

    std::unique_ptr<win::tabbox<type>> tabbox;
    std::unique_ptr<osd_notification<input_t>> osd;
    std::unique_ptr<kill_window<type>> window_killer;
    std::unique_ptr<win::user_actions_menu<type>> user_actions_menu;

    std::unique_ptr<desktop::screen_locker_watcher> screen_locker_watcher;
    std::unique_ptr<desktop::kde::kwin_impl<type>> dbus;

    std::vector<window_t> windows;
    std::unordered_map<uint32_t, window_t> windows_map;
    std::vector<win::x11::group<type>*> groups;

    stacking_state<window_t> stacking;

    std::optional<window_t> active_popup_client;
    std::optional<window_t> client_keys_client;
    std::optional<window_t> move_resize_window;

private:
    std::unique_ptr<base::x11::event_filter> edges_filter;
};

/**
 * Some fullscreen effects have to raise the screenedge on top of an input window, thus all windows
 * this function puts them back where they belong for regular use and is some cheap variant of
 * the regular propagate_clients function in that it completely ignores managed clients and
 * everything else and also does not update the NETWM property. Called from
 * Effects::destroyInputWindow so far.
 */
template<typename Space>
void stack_screen_edges_under_override_redirect(Space* space)
{
    if (!space->root_info) {
        return;
    }

    std::vector<xcb_window_t> windows;
    windows.push_back(space->root_info->supportWindow());

    auto const edges_wins = screen_edges_windows(*space->edges);
    windows.insert(windows.end(), edges_wins.begin(), edges_wins.end());

    base::x11::xcb::restack_windows(space->base.x11_data.connection, windows);
}

}
