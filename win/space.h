/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net.h"
#include "types.h"
#include "win.h"

#include "group.h"
#include "netinfo.h"
#include "options.h"

namespace KWin::win
{

template<typename Space>
void update_client_visibility_on_desktop_change(Space* space, uint newDesktop)
{
    for (auto const& toplevel : space->stackingOrder()) {
        auto client = qobject_cast<X11Client*>(toplevel);
        if (!client) {
            continue;
        }

        if (!client->isOnDesktop(newDesktop) && client != space->moveResizeClient()
            && client->isOnCurrentActivity()) {
            client->updateVisibility();
        }
    }

    // Now propagate the change, after hiding, before showing.
    if (rootInfo()) {
        rootInfo()->setCurrentDesktop(VirtualDesktopManager::self()->current());
    }

    if (auto move_resize_client = space->moveResizeClient()) {
        if (!move_resize_client->isOnDesktop(newDesktop)) {
            win::set_desktop(move_resize_client, newDesktop);
        }
    }

    auto const& stacking_order = space->stackingOrder();
    for (int i = stacking_order.size() - 1; i >= 0; --i) {
        auto client = qobject_cast<X11Client*>(stacking_order.at(i));
        if (!client) {
            continue;
        }
        if (client->isOnDesktop(newDesktop) && client->isOnCurrentActivity()) {
            client->updateVisibility();
        }
    }

    if (space->showingDesktop()) {
        // Do this only after desktop change to avoid flicker.
        space->setShowingDesktop(false);
    }
}

template<typename Space>
void update_tool_windows(Space* space, bool also_hide)
{
    // TODO: What if Client's transiency/group changes? should this be called too? (I'm paranoid, am
    // I not?)
    if (!options->isHideUtilityWindowsForInactive()) {
        for (auto const& client : space->allClientList()) {
            client->hideClient(false);
        }
        return;
    }

    Group const* group = nullptr;
    auto client = space->activeClient();

    // Go up in transiency hiearchy, if the top is found, only tool transients for the top
    // mainwindow will be shown; if a group transient is group, all tools in the group will be shown
    while (client != nullptr) {
        if (!client->isTransient()) {
            break;
        }
        if (client->groupTransient()) {
            group = client->group();
            break;
        }
        client = dynamic_cast<AbstractClient*>(client->control()->transient_lead());
    }

    // Use stacking order only to reduce flicker, it doesn't matter if block_stacking_updates == 0,
    // i.e. if it's not up to date.

    // SELI TODO: But maybe it should - what if a new client has been added that's not in stacking
    // order yet?
    std::vector<AbstractClient*> to_show;
    std::vector<AbstractClient*> to_hide;

    for (auto const& toplevel : space->stackingOrder()) {
        auto c = qobject_cast<AbstractClient*>(toplevel);
        if (!c) {
            continue;
        }

        if (is_utility(c) || is_menu(c) || is_toolbar(c)) {
            bool show = true;

            if (!c->isTransient()) {

                if (!c->group() || c->group()->members().size() == 1) {
                    // Has its own group, keep always visible
                    show = true;
                } else if (client != nullptr && c->group() == client->group()) {
                    show = true;
                } else {
                    show = false;
                }

            } else {
                if (group != nullptr && c->group() == group) {
                    show = true;
                } else if (client != nullptr && client->control()->has_transient(c, true)) {
                    show = true;
                } else {
                    show = false;
                }
            }

            if (!show && also_hide) {
                auto const& mainclients = c->mainClients();
                // Don't hide utility windows which are standalone(?) or
                // have e.g. kicker as mainwindow
                if (mainclients.isEmpty()) {
                    show = true;
                }
                for (auto const& client2 : mainclients) {
                    if (is_special_window(client2)) {
                        show = true;
                    }
                }
                if (!show) {
                    to_hide.push_back(c);
                }
            }

            if (show) {
                to_show.push_back(c);
            }
        }
    }

    // First show new ones, then hide.
    // From topmost.
    for (int i = to_show.size() - 1; i >= 0; --i) {
        // TODO: Since this is in stacking order, the order of taskbar entries changes :(
        to_show.at(i)->hideClient(false);
    }

    if (also_hide) {
        for (auto const& client : to_hide) {
            // From bottommost
            client->hideClient(true);
        }
        space->stopUpdateToolWindowsTimer();
    } else {
        // setActiveClient() is after called with NULL client, quickly followed
        // by setting a new client, which would result in flickering
        space->resetUpdateToolWindowsTimer();
    }
}

}
