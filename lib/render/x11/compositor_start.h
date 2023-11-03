/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/compositor_start.h>
#include <render/x11/compositor_selection_owner.h>
#include <render/x11/support_properties.h>

#include <xcb/composite.h>

namespace KWin::render::x11
{

// 2 sec which should be enough to restart the compositor.
constexpr auto compositor_lost_message_delay = 2000;

template<typename Compositor>
void compositor_claim_selection(Compositor& comp)
{
    if (!comp.selection_owner) {
        char selection_name[100];
        sprintf(selection_name, "_NET_WM_CM_S%d", comp.base.x11_data.screen_number);
        comp.selection_owner = std::make_unique<x11::compositor_selection_owner>(
            selection_name, comp.base.x11_data.connection, comp.base.x11_data.root_window);
        if (comp.selection_owner) {
            QObject::connect(comp.selection_owner.get(),
                             &x11::compositor_selection_owner::lostOwnership,
                             comp.qobject.get(),
                             [comp = &comp] { compositor_stop(*comp, false); });
        }
    }

    if (!comp.selection_owner) {
        // No X11 yet.
        return;
    }

    comp.selection_owner->own();
}

template<typename Compositor>
void compositor_claim(Compositor& comp)
{
    auto con = comp.base.x11_data.connection;
    if (!con) {
        comp.selection_owner = {};
        return;
    }
    compositor_claim_selection(comp);
    xcb_composite_redirect_subwindows(
        con, comp.base.x11_data.root_window, XCB_COMPOSITE_REDIRECT_MANUAL);
}

template<typename Compositor>
void compositor_setup(Compositor& comp)
{
    comp.unused_support_property_timer.setInterval(compositor_lost_message_delay);
    comp.unused_support_property_timer.setSingleShot(true);
    QObject::connect(&comp.unused_support_property_timer,
                     &QTimer::timeout,
                     comp.qobject.get(),
                     [&] { delete_unused_support_properties(comp); });
}

}
