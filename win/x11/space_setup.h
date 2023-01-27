/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "color_mapper.h"
#include "control_create.h"
#include "moving_window_filter.h"
#include "netinfo.h"
#include "placement.h"
#include "rules.h"
#include "space_event.h"
#include "sync_alarm_filter.h"

#include "base/x11/user_interaction_filter.h"
#include "base/x11/xcb/window.h"
#include "main.h"
#include "utils/blocker.h"
#include "win/desktop_space.h"
#include "win/space_areas_helpers.h"

#include <KStartupInfo>

namespace KWin::win::x11
{

inline static void select_wm_input_event_mask(base::x11::data const& data)
{
    uint32_t presentMask = 0;
    base::x11::xcb::window_attributes attr(data.connection, data.root_window);
    if (!attr.is_null()) {
        presentMask = attr->your_event_mask;
    }

    base::x11::xcb::select_input(
        data.connection,
        data.root_window,
        presentMask | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_PROPERTY_CHANGE
            | XCB_EVENT_MASK_COLOR_MAP_CHANGE | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
            | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE
            | // For NotifyDetailNone
            XCB_EVENT_MASK_EXPOSURE);
}

template<typename Space>
void init_space(Space& space)
{
    assert(space.base.x11_data.connection);

    space.atoms->retrieveHelpers();

    using color_mapper_t = color_mapper<Space>;
    space.color_mapper = std::make_unique<color_mapper_t>(space);
    QObject::connect(space.qobject.get(),
                     &Space::qobject_t::clientActivated,
                     space.color_mapper.get(),
                     &color_mapper_t::update);

    // Call this before XSelectInput() on the root window
    space.startup
        = new KStartupInfo(KStartupInfo::DisableKWinModule | KStartupInfo::AnnounceSilenceChanges,
                           space.qobject.get());

    auto const& x11_data = space.base.x11_data;

    // Select windowmanager privileges
    select_wm_input_event_mask(x11_data);

    if (space.base.operation_mode == base::operation_mode::x11) {
        space.m_wasUserInteractionFilter.reset(
            new base::x11::user_interaction_filter([&space] { mark_as_user_interaction(space); }));
        space.m_movingClientFilter.reset(new moving_window_filter(space));
    }
    if (base::x11::xcb::extensions::self()->is_sync_available()) {
        space.m_syncAlarmFilter = std::make_unique<sync_alarm_filter<Space>>(space);
    }

    // Needed for proper initialization of user_time in Client ctor
    base::x11::update_time_from_clock(space.base);

    const uint32_t nullFocusValues[] = {true};
    space.m_nullFocus = std::make_unique<base::x11::xcb::window>(x11_data.connection,
                                                                 x11_data.root_window,
                                                                 QRect(-1, -1, 1, 1),
                                                                 XCB_WINDOW_CLASS_INPUT_ONLY,
                                                                 XCB_CW_OVERRIDE_REDIRECT,
                                                                 nullFocusValues);
    space.m_nullFocus->map();

    space.root_info = x11::root_info<Space>::create(space);
    auto& vds = space.virtual_desktop_manager;
    vds->setRootInfo(space.root_info.get());
    space.root_info->activate();

    rules_setup_book(*space.rule_book, x11_data);
    QObject::connect(kwinApp(),
                     &Application::x11ConnectionChanged,
                     space.rule_book->qobject.get(),
                     [&space] { rules_setup_book(*space.rule_book, space.base.x11_data); });

    // TODO: only in X11 mode
    // Extra NETRootInfo instance in Client mode is needed to get the values of the properties
    NETRootInfo client_info(x11_data.connection, NET::ActiveWindow | NET::CurrentDesktop);
    if (!qApp->isSessionRestored()) {
        space.m_initialDesktop = client_info.currentDesktop();
        vds->setCurrent(space.m_initialDesktop);
    }

    // TODO: better value
    space.root_info->setActiveWindow(XCB_WINDOW_NONE);
    focus_to_null(space);

    if (!qApp->isSessionRestored())
        ++space.block_focus; // Because it will be set below

    {
        // Begin updates blocker block
        blocker block(space.stacking.order);

        base::x11::xcb::tree tree(x11_data.connection, x11_data.root_window);
        xcb_window_t* wins = xcb_query_tree_children(tree.data());

        std::vector<base::x11::xcb::window_attributes> windowAttributes;
        std::vector<base::x11::xcb::geometry> windowGeometries;

        // Request the attributes and geometries of all toplevel windows
        for (int i = 0; i < tree->children_len; i++) {
            windowAttributes.push_back(
                base::x11::xcb::window_attributes(x11_data.connection, wins[i]));
            windowGeometries.push_back(base::x11::xcb::geometry(x11_data.connection, wins[i]));
        }

        // Get the replies
        for (int i = 0; i < tree->children_len; i++) {
            base::x11::xcb::window_attributes attr(windowAttributes.at(i));

            if (attr.is_null()) {
                continue;
            }

            if (attr->override_redirect) {
                if (attr->map_state == XCB_MAP_STATE_VIEWABLE
                    && attr->_class != XCB_WINDOW_CLASS_INPUT_ONLY)
                    // ### This will request the attributes again
                    create_unmanaged_window(wins[i], space);
            } else if (attr->map_state != XCB_MAP_STATE_UNMAPPED) {
                if (Application::wasCrash()) {
                    fix_position_after_crash(space, wins[i], windowGeometries.at(i).data());
                }

                // ### This will request the attributes again
                create_controlled_window(wins[i], true, space);
            }
        }

        // Propagate clients, will really happen at the end of the updates blocker block
        space.stacking.order.update_count();

        save_old_output_sizes(space);
        update_space_areas(space);

        // NETWM spec says we have to set it to (0,0) if we don't support it
        NETPoint* viewports = new NETPoint[vds->count()];
        space.root_info->setDesktopViewport(vds->count(), *viewports);
        delete[] viewports;
        QRect geom;

        for (auto output : space.base.outputs) {
            geom |= output->geometry();
        }

        NETSize desktop_geometry;
        desktop_geometry.width = geom.width();
        desktop_geometry.height = geom.height();
        space.root_info->setDesktopGeometry(desktop_geometry);
        set_showing_desktop(space, false);

    } // End updates blocker block

    // TODO: only on X11?
    std::optional<typename Space::window_t> new_active_win;
    if (!qApp->isSessionRestored()) {
        --space.block_focus;
        if (auto win = find_controlled_window<typename Space::x11_window>(
                space, predicate_match::window, client_info.activeWindow())) {
            new_active_win = win;
        }
    }

    if (!new_active_win && !space.stacking.active && space.stacking.should_get_focus.empty()) {
        // No client activated in manage()
        if (!new_active_win) {
            new_active_win = win::top_client_on_desktop(space, vds->current(), nullptr);
        }
        if (!new_active_win) {
            new_active_win = win::find_desktop(&space, true, vds->current());
        }
    }
    if (new_active_win) {
        std::visit(overload{[&](auto&& win) { activate_window(space, *win); }}, *new_active_win);
    }
}

template<typename Space>
void clear_space(Space& space)
{
    using var_win = typename Space::window_t;

    space.stacking.order.lock();

    // Use stacking.order, so that kwin --replace keeps stacking order
    auto const stack = space.stacking.order.stack;

    // "mutex" the stackingorder, since anything trying to access it from now on will find
    // many dangeling pointers and crash
    space.stacking.order.stack.clear();

    // Only release windows on X11.
    auto const is_x11 = space.base.operation_mode == base::operation_mode::x11;

    for (auto it = stack.cbegin(), end = stack.cend(); it != end; ++it) {
        std::visit(overload{[&](typename Space::x11_window* win) {
                                if (win->remnant) {
                                    return;
                                }

                                release_window(win, is_x11);

                                // No removeClient() is called, it does more than just removing.
                                // However, remove from some lists to e.g. prevent
                                // performTransiencyCheck() from crashing.
                                remove_all(space.windows, var_win(win));
                            },
                            [](auto&&) {}},
                   *it);
    }

    for (auto const& unmanaged : get_unmanageds(space)) {
        std::visit(overload{[&](typename Space::x11_window* unmanaged) {
                                release_window(unmanaged, is_x11);
                            },
                            [](auto&&) {}},
                   unmanaged);
        remove_all(space.windows, unmanaged);
        remove_all(space.stacking.order.pre_stack, unmanaged);
    }

    space.root_info.reset();
    space.shape_helper_window.reset();
    space.stacking.order.unlock();
}

}
