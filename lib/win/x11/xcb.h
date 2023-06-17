/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/property.h"
#include "win/types.h"

namespace KWin::win::x11
{

template<typename Win>
base::x11::xcb::property fetch_wm_client_leader(Win const& win)
{
    return base::x11::xcb::property(win.space.base.x11_data.connection,
                                    false,
                                    win.xcb_windows.client,
                                    win.space.atoms->wm_client_leader,
                                    XCB_ATOM_WINDOW,
                                    0,
                                    10000);
}

template<typename Win>
void read_wm_client_leader(Win& win, base::x11::xcb::property& prop)
{
    win.m_wmClientLeader = prop.value<xcb_window_t>(win.xcb_windows.client);
}

template<typename Win>
base::x11::xcb::property fetch_skip_close_animation(Win&& win)
{
    return base::x11::xcb::property(win.space.base.x11_data.connection,
                                    false,
                                    win.xcb_windows.client,
                                    win.space.atoms->kde_skip_close_animation,
                                    XCB_ATOM_CARDINAL,
                                    0,
                                    1);
}

template<typename Win>
base::x11::xcb::property fetch_show_on_screen_edge(Win* win)
{
    return base::x11::xcb::property(win->space.base.x11_data.connection,
                                    false,
                                    win->xcb_windows.client,
                                    win->space.atoms->kde_screen_edge_show,
                                    XCB_ATOM_CARDINAL,
                                    0,
                                    1);
}

template<typename Win>
void read_show_on_screen_edge(Win* win, base::x11::xcb::property& property)
{
    auto const value = property.value<uint32_t>(static_cast<uint32_t>(electric_border::none));
    auto border = electric_border::none;

    switch (value & 0xFF) {
    case 0:
        border = electric_border::top;
        break;
    case 1:
        border = electric_border::right;
        break;
    case 2:
        border = electric_border::bottom;
        break;
    case 3:
        border = electric_border::left;
        break;
    }

    if (border != electric_border::none) {
        QObject::disconnect(win->notifiers.edge_geometry);

        auto reserve_edge = [win, border]() {
            if (win->space.edges->reserve(win, border)) {
                win->hideClient(true);
            } else {
                win->hideClient(false);
            }
        };

        reserve_edge();
        win->notifiers.edge_geometry = QObject::connect(win->qobject.get(),
                                                        &Win::qobject_t::frame_geometry_changed,
                                                        win->qobject.get(),
                                                        reserve_edge);
    } else if (!property.is_null() && property->type != XCB_ATOM_NONE) {
        // property value is incorrect, delete the property
        // so that the client knows that it is not hidden
        xcb_delete_property(win->space.base.x11_data.connection,
                            win->xcb_windows.client,
                            win->space.atoms->kde_screen_edge_show);
    } else {
        // restore
        QObject::disconnect(win->notifiers.edge_geometry);
        win->hideClient(false);
        win->space.edges->reserve(win, electric_border::none);
    }
}

template<typename Win>
void update_show_on_screen_edge(Win* win)
{
    auto property = fetch_show_on_screen_edge(win);
    read_show_on_screen_edge(win, property);
}

}
