/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/property.h"

namespace KWin::win::x11
{

template<typename Win>
base::x11::xcb::property fetch_wm_client_leader(Win const& win)
{
    return base::x11::xcb::property(
        false, win.xcb_window, win.space.atoms->wm_client_leader, XCB_ATOM_WINDOW, 0, 10000);
}

template<typename Win>
void read_wm_client_leader(Win& win, base::x11::xcb::property& prop)
{
    win.m_wmClientLeader = prop.value<xcb_window_t>(win.xcb_window);
}

template<typename Win>
base::x11::xcb::property fetch_skip_close_animation(Win&& win)
{
    return base::x11::xcb::property(
        false, win.xcb_window, win.space.atoms->kde_skip_close_animation, XCB_ATOM_CARDINAL, 0, 1);
}

template<typename Win>
base::x11::xcb::property fetch_first_in_tabbox(Win* win)
{
    auto& atoms = win->space.atoms;
    return base::x11::xcb::property(false,
                                    win->xcb_windows.client,
                                    atoms->kde_first_in_window_list,
                                    atoms->kde_first_in_window_list,
                                    0,
                                    1);
}

template<typename Win>
void read_first_in_tabbox(Win* win, base::x11::xcb::property& property)
{
    win->control->first_in_tabbox
        = property.to_bool(32, win->space.atoms->kde_first_in_window_list);
}

template<typename Win>
void update_first_in_tabbox(Win* win)
{
    // TODO: move into KWindowInfo
    auto property = fetch_first_in_tabbox(win);
    read_first_in_tabbox(win, property);
}

template<typename Win>
base::x11::xcb::property fetch_show_on_screen_edge(Win* win)
{
    return base::x11::xcb::property(
        false, win->xcb_window, win->space.atoms->kde_screen_edge_show, XCB_ATOM_CARDINAL, 0, 1);
}

template<typename Win>
void read_show_on_screen_edge(Win* win, base::x11::xcb::property& property)
{
    // value comes in two parts, edge in the lower byte
    // then the type in the upper byte
    // 0 = autohide
    // 1 = raise in front on activate

    auto const value = property.value<uint32_t>(ElectricNone);
    auto border = ElectricNone;

    switch (value & 0xFF) {
    case 0:
        border = ElectricTop;
        break;
    case 1:
        border = ElectricRight;
        break;
    case 2:
        border = ElectricBottom;
        break;
    case 3:
        border = ElectricLeft;
        break;
    }

    if (border != ElectricNone) {
        QObject::disconnect(win->connections.edge_remove);
        QObject::disconnect(win->connections.edge_geometry);
        auto successfullyHidden = false;

        if (((value >> 8) & 0xFF) == 1) {
            set_keep_below(win, true);

            // request could have failed due to user kwin rules
            successfullyHidden = win->control->keep_below;

            win->connections.edge_remove = QObject::connect(
                win->qobject.get(), &Win::qobject_t::keepBelowChanged, win->qobject.get(), [win]() {
                    if (!win->control->keep_below) {
                        win->space.edges->reserve(win, ElectricNone);
                    }
                });
        } else {
            win->hideClient(true);
            successfullyHidden = win->isHiddenInternal();

            win->connections.edge_geometry
                = QObject::connect(win->qobject.get(),
                                   &Win::qobject_t::frame_geometry_changed,
                                   win->qobject.get(),
                                   [win, border]() {
                                       win->hideClient(true);
                                       win->space.edges->reserve(win, border);
                                   });
        }

        if (successfullyHidden) {
            win->space.edges->reserve(win, border);
        } else {
            win->space.edges->reserve(win, ElectricNone);
        }
    } else if (!property.is_null() && property->type != XCB_ATOM_NONE) {
        // property value is incorrect, delete the property
        // so that the client knows that it is not hidden
        xcb_delete_property(connection(), win->xcb_window, win->space.atoms->kde_screen_edge_show);
    } else {
        // restore
        // TODO: add proper unreserve

        // this will call showOnScreenEdge to reset the state
        QObject::disconnect(win->connections.edge_geometry);
        win->space.edges->reserve(win, ElectricNone);
    }
}

template<typename Win>
void update_show_on_screen_edge(Win* win)
{
    auto property = fetch_show_on_screen_edge(win);
    read_show_on_screen_edge(win, property);
}

}
