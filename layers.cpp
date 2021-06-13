/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

// SELI zmenit doc

/*

 This file contains things relevant to stacking order and layers.

 Design:

 Normal unconstrained stacking order, as requested by the user (by clicking
 on windows to raise them, etc.), is in Workspace::unconstrained_stacking_order.
 That list shouldn't be used at all, except for building
 Workspace::stacking_order. The building is done
 in Workspace::constrainedStackingOrder(). Only Workspace::stackingOrder() should
 be used to get the stacking order, because it also checks the stacking order
 is up to date.
 All clients are also stored in Workspace::clients (except for isDesktop() clients,
 as those are very special, and are stored in Workspace::desktops), in the order
 the clients were created.

 Every window has one layer assigned in which it is. There are 7 layers,
 from bottom : DesktopLayer, BelowLayer, NormalLayer, DockLayer, AboveLayer, NotificationLayer,
 ActiveLayer, CriticalNotificationLayer, and OnScreenDisplayLayer (see also NETWM sect.7.10.).
 The layer a window is in depends on the window type, and on other things like whether the window
 is active. We extend the layers provided in NETWM by the NotificationLayer, OnScreenDisplayLayer,
 and CriticalNotificationLayer.
 The NoficationLayer contains notification windows which are kept above all windows except the active
 fullscreen window. The CriticalNotificationLayer contains notification windows which are important
 enough to keep them even above fullscreen windows. The OnScreenDisplayLayer is used for eg. volume
 and brightness change feedback and is kept above all windows since it provides immediate response
 to a user action.

 NET::Splash clients belong to the Normal layer. NET::TopMenu clients
 belong to Dock layer. Clients that are both NET::Dock and NET::KeepBelow
 are in the Normal layer in order to keep the 'allow window to cover
 the panel' Kicker setting to work as intended (this may look like a slight
 spec violation, but a) I have no better idea, b) the spec allows adjusting
 the stacking order if the WM thinks it's a good idea . We put all
 NET::KeepAbove above all Docks too, even though the spec suggests putting
 them in the same layer.

 Most transients are in the same layer as their mainwindow,
 see Workspace::constrainedStackingOrder(), they may also be in higher layers, but
 they should never be below their mainwindow.

 When some client attribute changes (above/below flag, transiency...),
 win::update_layer() should be called in order to make
 sure it's moved to the appropriate layer QList<X11Client *> if needed.

 Currently the things that affect client in which layer a client
 belongs: KeepAbove/Keep Below flags, window type, fullscreen
 state and whether the client is active, mainclient (transiency).

 Make sure updateStackingOrder() is called in order to make
 Workspace::stackingOrder() up to date and propagated to the world.
 Using Workspace::blockStackingUpdates() (or the StackingUpdatesBlocker
 helper class) it's possible to temporarily disable updates
 and the stacking order will be updated once after it's allowed again.

*/

#include "utils.h"
#include "workspace.h"
#include "tabbox.h"
#include "rules/rules.h"
#include "screens.h"
#include "effects.h"
#include "composite.h"
#include "screenedge.h"
#include "wayland_server.h"

#include "win/controlling.h"
#include "win/focuschain.h"
#include "win/internal_client.h"
#include "win/meta.h"
#include "win/net.h"
#include "win/remnant.h"
#include "win/screen.h"
#include "win/stacking.h"
#include "win/util.h"
#include "win/x11/control.h"
#include "win/x11/group.h"
#include "win/x11/hide.h"
#include "win/x11/netinfo.h"
#include "win/x11/window.h"

#include <QDebug>

namespace KWin
{

//*******************************
// Workspace
//*******************************

void Workspace::updateStackingOrder(bool propagate_new_clients)
{
    if (block_stacking_updates > 0) {
        if (propagate_new_clients)
            blocked_propagating_new_clients = true;
        return;
    }
    auto new_stacking_order = constrainedStackingOrder();
    bool changed = (force_restacking || new_stacking_order != stacking_order);
    force_restacking = false;
    stacking_order = new_stacking_order;
    if (changed || propagate_new_clients) {
        propagateClients(propagate_new_clients);
        markXStackingOrderAsDirty();
        Q_EMIT stackingOrderChanged();

        if (active_client)
            active_client->control->update_mouse_grab();
    }
}

/**
 * Some fullscreen effects have to raise the screenedge on top of an input window, thus all windows
 * this function puts them back where they belong for regular use and is some cheap variant of
 * the regular propagateClients function in that it completely ignores managed clients and everything
 * else and also does not update the NETWM property.
 * Called from Effects::destroyInputWindow so far.
 */
void Workspace::stackScreenEdgesUnderOverrideRedirect()
{
    if (!win::x11::rootInfo()) {
        return;
    }

    std::vector<xcb_window_t> windows;
    windows.push_back(win::x11::rootInfo()->supportWindow());

    auto const edges_wins = ScreenEdges::self()->windows();
    windows.insert(windows.end(), edges_wins.begin(), edges_wins.end());

    Xcb::restackWindows(windows);
}

/**
 * Propagates the managed clients to the world.
 * Called ONLY from updateStackingOrder().
 */
void Workspace::propagateClients(bool propagate_new_clients)
{
    if (!win::x11::rootInfo()) {
        return;
    }
    // restack the windows according to the stacking order
    // supportWindow > electric borders > clients > hidden clients
    std::vector<xcb_window_t> newWindowStack;

    // Stack all windows under the support window. The support window is
    // not used for anything (besides the NETWM property), and it's not shown,
    // but it was lowered after kwin startup. Stacking all clients below
    // it ensures that no client will be ever shown above override-redirect
    // windows (e.g. popups).
    newWindowStack.push_back(win::x11::rootInfo()->supportWindow());

    auto const edges_wins = ScreenEdges::self()->windows();
    newWindowStack.insert(newWindowStack.end(), edges_wins.begin(), edges_wins.end());

    newWindowStack.insert(newWindowStack.end(), manual_overlays.begin(), manual_overlays.end());

    // Twice the stacking-order size for inputWindow
    newWindowStack.reserve(newWindowStack.size() + 2 * stacking_order.size());

    for (int i = stacking_order.size() - 1; i >= 0; --i) {
        auto client = qobject_cast<win::x11::window*>(stacking_order.at(i));
        if (!client || win::x11::hidden_preview(client)) {
            continue;
        }

        if (client->xcb_windows.input) {
            // Stack the input window above the frame
            newWindowStack.push_back(client->xcb_windows.input);
        }

        newWindowStack.push_back(client->frameId());
    }

    // when having hidden previews, stack hidden windows below everything else
    // (as far as pure X stacking order is concerned), in order to avoid having
    // these windows that should be unmapped to interfere with other windows
    for (int i = stacking_order.size() - 1; i >= 0; --i) {
        auto client = qobject_cast<win::x11::window*>(stacking_order.at(i));
        if (!client || !win::x11::hidden_preview(client)) {
            continue;
        }
        newWindowStack.push_back(client->frameId());
    }
    // TODO isn't it too inefficient to restack always all clients?
    // TODO don't restack not visible windows?
    Q_ASSERT(newWindowStack.at(0) == win::x11::rootInfo()->supportWindow());
    Xcb::restackWindows(newWindowStack);

    int pos = 0;
    xcb_window_t *cl(nullptr);

    std::vector<win::x11::window*> x11_clients;
    for (auto const& client : allClientList()) {
        auto x11_client = qobject_cast<win::x11::window*>(client);
        if (x11_client) {
            x11_clients.push_back(x11_client);
        }
    }

    if (propagate_new_clients) {
        cl = new xcb_window_t[ manual_overlays.size() + x11_clients.size()];
        for (const auto win : manual_overlays) {
            cl[pos++] = win;
        }

        // TODO this is still not completely in the map order
        // TODO(romangg): can we make this more efficient (only looping once)?
        for (auto const& x11_client : x11_clients) {
            if (win::is_desktop(x11_client)) {
                cl[pos++] = x11_client->xcb_window();
            }
        }
        for (auto const& x11_client : x11_clients) {
            if (!win::is_desktop(x11_client)) {
                cl[pos++] = x11_client->xcb_window();
            }
        }

        win::x11::rootInfo()->setClientList(cl, pos);
        delete [] cl;
    }

    cl = new xcb_window_t[ manual_overlays.size() + stacking_order.size()];
    pos = 0;
    for (auto const& client : stacking_order) {
        if (auto x11_client = qobject_cast<win::x11::window*>(client)) {
            cl[pos++] = x11_client->xcb_window();
        }
    }
    for (auto const& win : manual_overlays) {
        cl[pos++] = win;
    }
    win::x11::rootInfo()->setClientListStacking(cl, pos);
    delete [] cl;
}

void Workspace::restoreSessionStackingOrder(win::x11::window* c)
{
    if (c->sm_stacking_order < 0) {
        return;
    }

    StackingUpdatesBlocker blocker(this);
    remove_all(unconstrained_stacking_order, c);

    for (auto it = unconstrained_stacking_order.begin();  // from bottom
            it != unconstrained_stacking_order.end();
            ++it) {
        auto current = qobject_cast<win::x11::window*>(*it);
        if (!current) {
            continue;
        }
        if (current->sm_stacking_order > c->sm_stacking_order) {
            unconstrained_stacking_order.insert(it, c);
            return;
        }
    }
    unconstrained_stacking_order.push_back(c);
}

/**
 * Returns a stacking order based upon \a list that fulfills certain contained.
 */
std::deque<Toplevel*> Workspace::constrainedStackingOrder()
{
    constexpr size_t layer_count = static_cast<int>(win::layer::count);
    std::deque<Toplevel*> layer[layer_count];

    // build the order from layers
    QVector< QMap<win::x11::Group*, win::layer> > minimum_layer(qMax(screens()->count(), 1));

    for (auto const& window : unconstrained_stacking_order) {
        auto l = window->layer();

        auto const screen = window->screen();
        auto c = qobject_cast<win::x11::window*>(window);

        QMap< win::x11::Group*, win::layer >::iterator mLayer = minimum_layer[screen].find(c ? c->group() : nullptr);
        if (mLayer != minimum_layer[screen].end()) {
            // If a window is raised above some other window in the same window group
            // which is in the ActiveLayer (i.e. it's fulscreened), make sure it stays
            // above that window (see #95731).
            if (*mLayer == win::layer::active
                    && (static_cast<int>(l) > static_cast<int>(win::layer::below))) {
                l = win::layer::active;
            }
            *mLayer = l;
        } else if (c) {
            minimum_layer[screen].insertMulti(c->group(), l);
        }
        layer[static_cast<size_t>(l)].push_back(window);
    }

    std::vector<Toplevel*> preliminary_stack;
    std::deque<Toplevel*> stack;

    for (auto lay = static_cast<size_t>(win::layer::first); lay < layer_count; ++lay) {
        preliminary_stack.insert(preliminary_stack.end(), layer[lay].begin(), layer[lay].end());
    }

    auto child_restack = [this](auto lead, auto child) {
        // Tells if a transient child should be restacked directly above its lead.
        if (lead->layer() < child->layer()) {
            // Child will be in a layer above the lead and should not be pulled down from that.
            return false;
        }
        if (child->remnant()) {
            return win::keep_deleted_transient_above(lead, child);
        }
        return win::keep_transient_above(lead, child);
    };

    auto append_children = [this, &child_restack](Toplevel* window, std::deque<Toplevel*>& list) {
        auto impl = [this, &child_restack](
                        Toplevel* window, std::deque<Toplevel*>& list, auto& impl_ref) {
            auto const children = window->transient()->children;
            if (!children.size()) {
                return;
            }

            auto stacked_next = ensureStackingOrder(children);
            std::deque<Toplevel*> stacked;

            // Append children by one first-level child after the other but between them any
            // transient children of each first-level child (acts recursively).
            for (auto child : stacked_next) {
                // Transients to multiple leads are pushed to the very end.
                if (!child_restack(window, child)) {
                    continue;
                }
                remove_all(list, child);

                stacked.push_back(child);
                impl_ref(child, stacked, impl_ref);
            }

            list.insert(list.end(), stacked.begin(), stacked.end());
        };

        impl(window, list, impl);
    };

    for (auto const& window : preliminary_stack) {
        if (auto const leads = window->transient()->leads();
            std::find_if(leads.cbegin(),
                         leads.cend(),
                         [window, child_restack](auto lead) { return child_restack(lead, window); })
            != leads.cend()) {
            // Transient children that must be pushed above at least one of its leads are inserted
            // with append_children.
            continue;
        }

        assert(!contains(stack, window));
        stack.push_back(window);
        append_children(window, stack);
    }

    return stack;
}

void Workspace::blockStackingUpdates(bool block)
{
    if (block) {
        if (block_stacking_updates == 0)
            blocked_propagating_new_clients = false;
        ++block_stacking_updates;
    } else if (--block_stacking_updates == 0) {
        updateStackingOrder(blocked_propagating_new_clients);
        emit blockStackingUpdatesEnded();
    }
}

// Ensure list is in stacking order
std::deque<win::x11::window*> Workspace::ensureStackingOrder(std::vector<win::x11::window*> const& list) const
{
    return win::ensure_stacking_order_in_list(stacking_order, list);
}

std::deque<Toplevel*> Workspace::ensureStackingOrder(std::vector<Toplevel*> const& list) const
{
    return win::ensure_stacking_order_in_list(stacking_order, list);
}

// Returns all windows in their stacking order on the root window.
std::deque<Toplevel*> const& Workspace::xStackingOrder() const
{
    if (m_xStackingDirty) {
        const_cast<Workspace*>(this)->updateXStackingOrder();
    }
    return x_stacking;
}

void Workspace::updateXStackingOrder()
{
    // use our own stacking order, not the X one, as they may differ
    x_stacking = stacking_order;

    if (m_xStackingQueryTree && !m_xStackingQueryTree->isNull()) {
        std::unique_ptr<Xcb::Tree> tree{std::move(m_xStackingQueryTree)};
        xcb_window_t *windows = tree->children();
        const auto count = tree->data()->children_len;

        auto const unmanageds = unmanagedList();
        auto foundUnmanagedCount = unmanageds.size();
        for (size_t i = 0; i < count; ++i) {
            for (auto const& u : unmanageds) {
                if (u->xcb_window() == windows[i]) {
                    x_stacking.push_back(u);
                    foundUnmanagedCount--;
                    break;
                }
            }
            if (foundUnmanagedCount == 0) {
                break;
            }
        }
    }

    for (auto const& toplevel : workspace()->windows()) {
        auto internal = qobject_cast<win::InternalClient*>(toplevel);
        if (internal && internal->isShown()) {
            x_stacking.push_back(internal);
        }
    }

    m_xStackingDirty = false;
}

}
