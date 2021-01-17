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
 Workspace::updateClientLayer() should be called in order to make
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
#include "focuschain.h"
#include "netinfo.h"
#include "workspace.h"
#include "tabbox.h"
#include "group.h"
#include "rules/rules.h"
#include "screens.h"
#include "effects.h"
#include "composite.h"
#include "screenedge.h"
#include "wayland_server.h"
#include "internal_client.h"

#include "win/controlling.h"
#include "win/meta.h"
#include "win/net.h"
#include "win/remnant.h"
#include "win/screen.h"
#include "win/stacking.h"
#include "win/util.h"
#include "win/x11/control.h"
#include "win/x11/hide.h"
#include "win/x11/window.h"

#include <QDebug>

namespace KWin
{

//*******************************
// Workspace
//*******************************

void Workspace::updateClientLayer(Toplevel *window)
{
    if (window) {
        win::update_layer(window);
    }
}

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
        emit stackingOrderChanged();
        if (m_compositor) {
            m_compositor->addRepaintFull();
        }

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
    if (!rootInfo()) {
        return;
    }

    std::vector<xcb_window_t> windows;
    windows.push_back(rootInfo()->supportWindow());

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
    if (!rootInfo()) {
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
    newWindowStack.push_back(rootInfo()->supportWindow());

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
    Q_ASSERT(newWindowStack.at(0) == rootInfo()->supportWindow());
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

        rootInfo()->setClientList(cl, pos);
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
    rootInfo()->setClientListStacking(cl, pos);
    delete [] cl;

    // Make the cached stacking order invalid here, in case we need the new stacking order before we get
    // the matching event, due to X being asynchronous.
    markXStackingOrderAsDirty();
}

/**
 * Returns topmost visible client. Windows on the dock, the desktop
 * or of any other special kind are excluded. Also if the window
 * doesn't accept focus it's excluded.
 */
// TODO misleading name for this method, too many slightly different ways to use it
Toplevel* Workspace::topClientOnDesktop(int desktop, int screen, bool unconstrained, bool only_normal) const
{
// TODO    Q_ASSERT( block_stacking_updates == 0 );
    std::deque<Toplevel*> list;
    if (!unconstrained)
        list = stacking_order;
    else
        list = unconstrained_stacking_order;
    for (int i = list.size() - 1;
            i >= 0;
            --i) {
        auto c = list.at(i);
        if (!c) {
            continue;
        }
        if (c->isOnDesktop(desktop) && c->isShown(false) && c->isOnCurrentActivity()) {
            if (screen != -1 && c->screen() != screen)
                continue;
            if (!only_normal)
                return c;
            if (win::wants_tab_focus(c) && !win::is_special_window(c))
                return c;
        }
    }
    return nullptr;
}

Toplevel* Workspace::findDesktop(bool topmost, int desktop) const
{
// TODO    Q_ASSERT( block_stacking_updates == 0 );
    if (topmost) {
        for (int i = stacking_order.size() - 1; i >= 0; i--) {
            auto window = stacking_order.at(i);
            if (window->control && window->isOnDesktop(desktop) && win::is_desktop(window)
                    && window->isShown(true)) {
                return window;
            }
        }
    } else { // bottom-most
        for (auto const& window : stacking_order) {
            if (window->control && window->isOnDesktop(desktop) && win::is_desktop(window)
                    && window->isShown(true)) {
                return window;
            }
        }
    }
    return nullptr;
}

void Workspace::raiseOrLowerClient(Toplevel *window)
{
    if (!window) {
        return;
    }

    Toplevel* topmost = nullptr;

    if (most_recently_raised && contains(stacking_order, most_recently_raised) &&
            most_recently_raised->isShown(true) && window->isOnCurrentDesktop()) {
        topmost = most_recently_raised;
    } else {
        topmost = topClientOnDesktop(window->isOnAllDesktops()
                                         ? VirtualDesktopManager::self()->current()
                                         : window->desktop(),
                                     options->isSeparateScreenFocus() ? window->screen() : -1);
    }

    if (window == topmost) {
        lower_window(window);
    } else {
        raise_window(window);
    }
}

void Workspace::lower_window(Toplevel* window)
{
    assert(window->control);

    auto do_lower = [this](Toplevel* win) {
        win->control->cancel_auto_raise();

        StackingUpdatesBlocker blocker(this);

        remove_all(unconstrained_stacking_order, win);
        unconstrained_stacking_order.push_front(win);

        return blocker;
    };
    auto cleanup = [this](Toplevel* win) {
        if (win == most_recently_raised) {
            most_recently_raised = nullptr;
        }
    };

    auto blocker = do_lower(window);

    if (window->isTransient() && window->group()) {
        // Lower also all windows in the group, in reversed stacking order.
        auto const wins = ensureStackingOrder(window->group()->members());

        for (auto it = wins.crbegin(); it != wins.crend(); it++) {
            auto gwin = *it;
            if (gwin == window) {
                continue;
            }

            assert(gwin->control);
            do_lower(gwin);
            cleanup(gwin);
        }
    }

    cleanup(window);
}

void Workspace::lowerClientWithinApplication(Toplevel* window)
{
    if (!window) {
        return;
    }

    window->control->cancel_auto_raise();

    StackingUpdatesBlocker blocker(this);

    remove_all(unconstrained_stacking_order, window);

    bool lowered = false;
    // first try to put it below the bottom-most window of the application
    for (auto it = unconstrained_stacking_order.begin();
            it != unconstrained_stacking_order.end();
            ++it) {
        auto const& client = *it;
        if (!client) {
            continue;
        }
        if (win::belong_to_same_client(client, window)) {
            unconstrained_stacking_order.insert(it, window);
            lowered = true;
            break;
        }
    }
    if (!lowered)
        unconstrained_stacking_order.push_front(window);
    // ignore mainwindows
}

void Workspace::raise_window(Toplevel* window)
{
    if (!window) {
        return;
    }

    auto prepare = [this](Toplevel* window) {
        assert(window->control);
        window->control->cancel_auto_raise();
        return StackingUpdatesBlocker(this);
    };
    auto do_raise = [this](Toplevel* window) {
        remove_all(unconstrained_stacking_order, window);
        unconstrained_stacking_order.push_back(window);

        if (!win::is_special_window(window)) {
            most_recently_raised = window;
        }
    };

    auto blocker = prepare(window);

    if (window->isTransient()) {
        // Also raise all leads.
        std::vector<Toplevel*> leads;

        for (auto lead : window->transient()->leads()) {
            while (lead) {
                if (!contains(leads, lead)) {
                    leads.push_back(lead);
                }
                lead = lead->transient()->lead();
            }
        }

        auto stacked_leads = ensureStackingOrder(leads);

        for (auto lead : stacked_leads) {
            if (!lead->control) {
                // Might be without control, at least on X11 this can happen (latte-dock settings).
                continue;
            }
            auto blocker = prepare(lead);
            do_raise(lead);
        }
    }

    do_raise(window);
}

void Workspace::raiseClientWithinApplication(Toplevel* window)
{
    if (!window) {
        return;
    }

    window->control->cancel_auto_raise();

    StackingUpdatesBlocker blocker(this);
    // ignore mainwindows

    // first try to put it above the top-most window of the application
    for (int i = unconstrained_stacking_order.size() - 1; i > -1 ; --i) {
        auto other = unconstrained_stacking_order.at(i);
        if (!other) {
            continue;
        }
        if (other == window) {
            // Don't lower it just because it asked to be raised.
            return;
        }
        if (win::belong_to_same_client(other, window)) {
            remove_all(unconstrained_stacking_order, window);
            auto it = find(unconstrained_stacking_order, other);
            assert(it != unconstrained_stacking_order.end());
            // Insert after the found one.
            unconstrained_stacking_order.insert(it + 1, window);
            break;
        }
    }
}

void Workspace::raiseClientRequest(Toplevel* window, NET::RequestSource src, xcb_timestamp_t timestamp)
{
    if (src == NET::FromTool || allowFullClientRaising(window, timestamp)) {
        raise_window(window);
    } else {
        raiseClientWithinApplication(window);
        win::set_demands_attention(window, true);
    }
}

void Workspace::lowerClientRequest(KWin::win::x11::window* c, NET::RequestSource src, xcb_timestamp_t /*timestamp*/)
{
    // If the client has support for all this focus stealing prevention stuff,
    // do only lowering within the application, as that's the more logical
    // variant of lowering when application requests it.
    // No demanding of attention here of course.
    if (src == NET::FromTool || !win::x11::has_user_time_support(c)) {
        lower_window(c);
    } else {
        lowerClientWithinApplication(c);
    }
}

void Workspace::lowerClientRequest(Toplevel* window)
{
    lowerClientWithinApplication(window);
}

void Workspace::restack(Toplevel* window, Toplevel* under, bool force)
{
    assert(contains(unconstrained_stacking_order, under));

    if (!force && !win::belong_to_same_client(under, window)) {
         // put in the stacking order below _all_ windows belonging to the active application
        for (size_t i = 0; i < unconstrained_stacking_order.size(); ++i) {
            auto other = unconstrained_stacking_order.at(i);
            if (other->control && other->layer() == window->layer() &&
                    win::belong_to_same_client(under, other)) {
                under = (window == other) ? nullptr : other;
                break;
            }
        }
    }
    if (under) {
        remove_all(unconstrained_stacking_order, window);
        auto it = find(unconstrained_stacking_order, under);
        unconstrained_stacking_order.insert(it, window);
    }

    assert(contains(unconstrained_stacking_order, window));
    FocusChain::self()->moveAfterClient(window, under);
    updateStackingOrder();
}

void Workspace::restackClientUnderActive(Toplevel* window)
{
    if (!active_client || active_client == window || active_client->layer() != window->layer()) {
        raise_window(window);
        return;
    }
    restack(window, active_client);
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
    QVector< QMap<Group*, win::layer> > minimum_layer(qMax(screens()->count(), 1));

    for (auto const& window : unconstrained_stacking_order) {
        auto l = window->layer();

        auto const screen = window->screen();
        auto c = qobject_cast<win::x11::window*>(window);

        QMap< Group*, win::layer >::iterator mLayer = minimum_layer[screen].find(c ? c->group() : nullptr);
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
            return keepDeletedTransientAbove(lead, child);
        }
        return keepTransientAbove(lead, child);
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
        if (effects) {
            static_cast<EffectsHandlerImpl*>(effects)->checkInputWindowStacking();
        }
    }
}

namespace {
template <class T, class R = T>
std::deque<R*> ensureStackingOrderInList(std::deque<Toplevel*> const& stackingOrder, std::vector<T*> const& list)
{
    static_assert(std::is_base_of<Toplevel, T>::value,
                 "U must be derived from T");
// TODO    Q_ASSERT( block_stacking_updates == 0 );

    if (!list.size()) {
        return std::deque<R*>();
    }
    if (list.size() < 2) {
        return std::deque<R*>({qobject_cast<R*>(list.at(0))});
    }

    // TODO is this worth optimizing?
    std::deque<R*> result;
    for (auto c : list) {
        result.push_back(qobject_cast<R*>(c));
    }
    for (auto it = stackingOrder.begin();
            it != stackingOrder.end();
            ++it) {
        R *c = qobject_cast<R*>(*it);
        if (!c) {
            continue;
        }
        if (contains(result, c)) {
            remove_all(result, c);
            result.push_back(c);
        }
    }
    return result;
}
}

// Ensure list is in stacking order
std::deque<win::x11::window*> Workspace::ensureStackingOrder(std::vector<win::x11::window*> const& list) const
{
    return ensureStackingOrderInList(stacking_order, list);
}

std::deque<Toplevel*> Workspace::ensureStackingOrder(std::vector<Toplevel*> const& list) const
{
    return ensureStackingOrderInList(stacking_order, list);
}

// check whether a transient should be actually kept above its mainwindow
// there may be some special cases where this rule shouldn't be enfored
bool Workspace::keepTransientAbove(Toplevel const* mainwindow, Toplevel const* transient)
{
    if (transient->transient()->annexed) {
        return true;
    }
    // #93832 - don't keep splashscreens above dialogs
    if (win::is_splash(transient) && win::is_dialog(mainwindow))
        return false;
    // This is rather a hack for #76026. Don't keep non-modal dialogs above
    // the mainwindow, but only if they're group transient (since only such dialogs
    // have taskbar entry in Kicker). A proper way of doing this (both kwin and kicker)
    // needs to be found.
    if (win::is_dialog(transient) && !transient->transient()->modal() && transient->groupTransient())
        return false;
    // #63223 - don't keep transients above docks, because the dock is kept high,
    // and e.g. dialogs for them would be too high too
    // ignore this if the transient has a placement hint which indicates it should go above it's parent
    if (win::is_dock(mainwindow))
        return false;
    return true;
}

bool Workspace::keepDeletedTransientAbove(Toplevel const* mainWindow, Toplevel const* transient) const
{
    assert(transient->remnant());

    // #93832 - Don't keep splashscreens above dialogs.
    if (win::is_splash(transient) && win::is_dialog(mainWindow)) {
        return false;
    }

    if (transient->remnant()->was_x11_client) {
        // If a group transient was active, we should keep it above no matter
        // what, because at the time when the transient was closed, it was above
        // the main window.
        if (transient->remnant()->was_group_transient && transient->remnant()->was_active) {
            return true;
        }

        // This is rather a hack for #76026. Don't keep non-modal dialogs above
        // the mainwindow, but only if they're group transient (since only such
        // dialogs have taskbar entry in Kicker). A proper way of doing this
        // (both kwin and kicker) needs to be found.
        if (transient->remnant()->was_group_transient && win::is_dialog(transient)
                && !transient->transient()->modal()) {
            return false;
        }

        // #63223 - Don't keep transients above docks, because the dock is kept
        // high, and e.g. dialogs for them would be too high too.
        if (win::is_dock(mainWindow)) {
            return false;
        }
    }

    return true;
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
    x_stacking.clear();
    std::unique_ptr<Xcb::Tree> tree{std::move(m_xStackingQueryTree)};
    // use our own stacking order, not the X one, as they may differ
    for (auto const& window : stacking_order) {
        x_stacking.push_back(window);
    }

    if (tree && !tree->isNull()) {
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
        auto internal = qobject_cast<InternalClient*>(toplevel);
        if (internal && internal->isShown(false)) {
            x_stacking.push_back(internal);
        }
    }

    m_xStackingDirty = false;
}

}
