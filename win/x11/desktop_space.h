/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "hide.h"
#include "netinfo.h"
#include "window.h"

namespace KWin::win::x11
{

inline void handle_desktop_resize(QSize const& size)
{
    if (!x11::rootInfo()) {
        return;
    }

    NETSize desktop_geometry;
    desktop_geometry.width = size.width();
    desktop_geometry.height = size.height();
    x11::rootInfo()->setDesktopGeometry(desktop_geometry);
}

template<typename Space>
void popagate_desktop_change(Space& space, uint desktop)
{
    for (auto const& toplevel : space.stacking_order->stack) {
        auto client = dynamic_cast<x11::window*>(toplevel);
        if (!client || !client->control) {
            continue;
        }

        if (!client->isOnDesktop(desktop) && client != space.move_resize_window) {
            update_visibility(client);
        }
    }

    // Now propagate the change, after hiding, before showing.
    if (x11::rootInfo()) {
        x11::rootInfo()->setCurrentDesktop(space.virtual_desktop_manager->current());
    }

    auto const& list = space.stacking_order->stack;
    for (int i = list.size() - 1; i >= 0; --i) {
        auto client = dynamic_cast<x11::window*>(list.at(i));
        if (!client || !client->control) {
            continue;
        }
        if (client->isOnDesktop(desktop)) {
            update_visibility(client);
        }
    }
}

}
