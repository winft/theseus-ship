/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "desktop_space.h"
#include "screen_edge.h"
#include "screen_edges_filter.h"
#include "space_areas.h"
#include "space_setup.h"
#include "window.h"

#include "base/x11/xcb/helpers.h"
#include "debug/console/x11/x11_console.h"
#include "input/x11/platform.h"
#include "input/x11/redirect.h"
#include "scripting/platform.h"
#include "utils/blocker.h"
#include "win/desktop_space.h"
#include "win/internal_window.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/stacking_order.h"

#include <vector>

namespace KWin::win::x11
{

template<typename Base>
class space : public win::space
{
public:
    using base_t = Base;
    using type = space<Base>;
    using window_t = Toplevel<type>;
    using x11_window = window<type>;

    // TODO(romangg): Remove once we can rely on Space::x11_window always being the group window.
    using x11_group_window = x11_window;
    using input_t = typename Base::input_t;

    // Not used on X11.
    // TODO(romangg): Make our function templates independent of this type and remove it.
    using internal_window_t = win::internal_window<type>;

    space(Base& base)
        : base{base}
        , outline{render::outline::create(*base.render->compositor,
                                          [this] {
                                              return render::create_outline_visual(
                                                  *this->base.render->compositor, *outline);
                                          })}
        , deco{std::make_unique<deco::bridge<type>>(*this)}
        , appmenu{std::make_unique<dbus::appmenu>(dbus::create_appmenu_callbacks(*this))}
        , user_actions_menu{std::make_unique<win::user_actions_menu<type>>(*this)}
    {
        win::init_space(*this);

        singleton_interface::get_current_output_geometry = [this] {
            auto output = get_current_output(*this);
            return output ? output->geometry() : QRect();
        };

        if (base.input) {
            this->input = std::make_unique<typename input_t::redirect_t>(*base.input, *this);
        }

        atoms = std::make_unique<base::x11::atoms>(connection());
        edges = std::make_unique<edger_t>(*this);
        dbus = std::make_unique<base::dbus::kwin_impl<type>>(*this);

        QObject::connect(virtual_desktop_manager->qobject.get(),
                         &virtual_desktop_manager_qobject::desktopRemoved,
                         qobject.get(),
                         [this] {
                             auto const desktop_count
                                 = static_cast<int>(virtual_desktop_manager->count());
                             for (auto const& window : windows) {
                                 if (!window->control) {
                                     continue;
                                 }
                                 if (window->isOnAllDesktops()) {
                                     continue;
                                 }
                                 if (window->desktop() <= desktop_count) {
                                     continue;
                                 }
                                 send_window_to_desktop(*this, window, desktop_count, true);
                             }
                         });

        QObject::connect(&base, &base::platform::topology_changed, qobject.get(), [this] {
            auto& comp = this->base.render->compositor;
            if (!comp->scene) {
                return;
            }
            // desktopResized() should take care of when the size or
            // shape of the desktop has changed, but we also want to
            // catch refresh rate changes
            //
            // TODO: is this still necessary since we get the maximal refresh rate now
            // dynamically?
            comp->reinitialize();
        });

        x11::init_space(*this);
    }

    ~space() override
    {
        win::clear_space(*this);
    }

    void resize(QSize const& size) override
    {
        handle_desktop_resize(root_info.get(), size);
        win::handle_desktop_resize(*this, size);
    }

    void handle_desktop_changed(uint desktop) override
    {
        x11::popagate_desktop_change(*this, desktop);
    }

    /// On X11 an internal window is an unmanaged and mapped by the window id.
    window_t* findInternal(QWindow* window) const
    {
        if (!window) {
            return nullptr;
        }
        return find_unmanaged<x11_window>(*this, window->winId());
    }

    QRect get_icon_geometry(window_t const* /*win*/) const
    {
        return {};
    }

    using edger_t = screen_edger<type>;
    std::unique_ptr<win::screen_edge<edger_t>> create_screen_edge(edger_t& edger)
    {
        if (!edges_filter) {
            edges_filter = std::make_unique<screen_edges_filter<space<Base>>>(*this);
        }
        return std::make_unique<x11::screen_edge<edger_t>>(&edger, *atoms);
    }

    void update_space_area_from_windows(QRect const& desktop_area,
                                        std::vector<QRect> const& screens_geos,
                                        win::space_areas& areas) override
    {
        for (auto const& window : windows) {
            if (!window->control) {
                continue;
            }
            if (auto x11_win = dynamic_cast<x11_window*>(window)) {
                update_space_areas(x11_win, desktop_area, screens_geos, areas);
            }
        }
    }

    void show_debug_console() override
    {
        auto console = new debug::x11_console(*this);
        console->show();
    }

    Base& base;

    std::unique_ptr<scripting::platform<type>> scripting;
    std::unique_ptr<render::outline> outline;
    std::unique_ptr<edger_t> edges;
    std::unique_ptr<deco::bridge<type>> deco;
    std::unique_ptr<dbus::appmenu> appmenu;
    std::unique_ptr<x11::root_info<space>> root_info;
    std::unique_ptr<x11::color_mapper<type>> color_mapper;

    std::unique_ptr<typename input_t::redirect_t> input;

    std::unique_ptr<win::tabbox<type>> tabbox;
    std::unique_ptr<osd_notification<input_t>> osd;
    std::unique_ptr<kill_window<type>> window_killer;
    std::unique_ptr<win::user_actions_menu<type>> user_actions_menu;
    std::unique_ptr<base::dbus::kwin_impl<type>> dbus;

    std::vector<window_t*> windows;
    std::unordered_map<uint32_t, window_t*> windows_map;
    std::vector<win::x11::group<type>*> groups;

    stacking_state<window_t> stacking;

    window_t* active_popup_client{nullptr};
    window_t* client_keys_client{nullptr};
    window_t* move_resize_window{nullptr};

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

    auto const edges_wins = space->edges->windows();
    windows.insert(windows.end(), edges_wins.begin(), edges_wins.end());

    base::x11::xcb::restack_windows(windows);
}

}
