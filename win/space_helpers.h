/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "meta.h"
#include "net.h"
#include "screen.h"
#include "stacking_order.h"
#include "transient.h"
#include "types.h"
#include "x11/netinfo.h"

namespace KWin::win
{

template<typename Space>
void update_client_visibility_on_desktop_change(Space* space, uint newDesktop)
{
    for (auto const& toplevel : space->stacking_order->sorted()) {
        auto client = qobject_cast<x11::window*>(toplevel);
        if (!client || !client->control) {
            continue;
        }

        if (!client->isOnDesktop(newDesktop) && client != space->moveResizeClient()) {
            update_visibility(client);
        }
    }

    // Now propagate the change, after hiding, before showing.
    if (x11::rootInfo()) {
        x11::rootInfo()->setCurrentDesktop(space->virtual_desktop_manager->current());
    }

    if (auto move_resize_client = space->moveResizeClient()) {
        if (!move_resize_client->isOnDesktop(newDesktop)) {
            win::set_desktop(move_resize_client, newDesktop);
        }
    }

    auto const& list = space->stacking_order->sorted();
    for (int i = list.size() - 1; i >= 0; --i) {
        auto client = qobject_cast<x11::window*>(list.at(i));
        if (!client || !client->control) {
            continue;
        }
        if (client->isOnDesktop(newDesktop)) {
            update_visibility(client);
        }
    }

    if (space->showingDesktop()) {
        // Do this only after desktop change to avoid flicker.
        space->setShowingDesktop(false);
    }
}

}
