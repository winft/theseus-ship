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
#include "x11client.h"
#include "focuschain.h"
#include "netinfo.h"
#include "workspace.h"
#include "tabbox.h"
#include "group.h"
#include "rules/rules.h"
#include "screens.h"
#include "unmanaged.h"
#include "deleted.h"
#include "effects.h"
#include "composite.h"
#include "screenedge.h"
#include "xdgshellclient.h"
#include "wayland_server.h"
#include "win/win.h"
#include "internal_client.h"

#include <QDebug>

namespace KWin
{

//*******************************
// Workspace
//*******************************

void Workspace::updateClientLayer(AbstractClient* c)
{
    if (c)
        win::update_layer(c);
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
            active_client->control()->update_mouse_grab();
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
        X11Client *client = qobject_cast<X11Client *>(stacking_order.at(i));
        if (!client || client->hiddenPreview()) {
            continue;
        }

        if (client->inputId())
            // Stack the input window above the frame
            newWindowStack.push_back(client->inputId());

        newWindowStack.push_back(client->frameId());
    }

    // when having hidden previews, stack hidden windows below everything else
    // (as far as pure X stacking order is concerned), in order to avoid having
    // these windows that should be unmapped to interfere with other windows
    for (int i = stacking_order.size() - 1; i >= 0; --i) {
        X11Client *client = qobject_cast<X11Client *>(stacking_order.at(i));
        if (!client || !client->hiddenPreview())
            continue;
        newWindowStack.push_back(client->frameId());
    }
    // TODO isn't it too inefficient to restack always all clients?
    // TODO don't restack not visible windows?
    Q_ASSERT(newWindowStack.at(0) == rootInfo()->supportWindow());
    Xcb::restackWindows(newWindowStack);

    int pos = 0;
    xcb_window_t *cl(nullptr);
    if (propagate_new_clients) {
        cl = new xcb_window_t[ manual_overlays.size() + desktops.size() + clients.size()];
        for (const auto win : manual_overlays) {
            cl[pos++] = win;
        }
        // TODO this is still not completely in the map order
        for (auto it = desktops.cbegin(); it != desktops.cend(); ++it) {
            cl[pos++] = (*it)->window();
        }
        for (auto it = clients.cbegin(); it != clients.cend(); ++it) {
            cl[pos++] = (*it)->window();
        }
        rootInfo()->setClientList(cl, pos);
        delete [] cl;
    }

    cl = new xcb_window_t[ manual_overlays.size() + stacking_order.size()];
    pos = 0;
    for (auto const& client : stacking_order) {
        if (auto x11_client = qobject_cast<X11Client*>(client)) {
            cl[pos++] = x11_client->window();
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
AbstractClient* Workspace::topClientOnDesktop(int desktop, int screen, bool unconstrained, bool only_normal) const
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
        AbstractClient *c = qobject_cast<AbstractClient*>(list.at(i));
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

AbstractClient* Workspace::findDesktop(bool topmost, int desktop) const
{
// TODO    Q_ASSERT( block_stacking_updates == 0 );
    if (topmost) {
        for (int i = stacking_order.size() - 1; i >= 0; i--) {
            AbstractClient *c = qobject_cast<AbstractClient*>(stacking_order.at(i));
            if (c && c->isOnDesktop(desktop) && win::is_desktop(c)
                    && c->isShown(true))
                return c;
        }
    } else { // bottom-most
        for (auto const& c : stacking_order) {
            auto client = qobject_cast<AbstractClient*>(c);
            if (client && c->isOnDesktop(desktop) && win::is_desktop(c)
                    && client->isShown(true)) {
                return client;
            }
        }
    }
    return nullptr;
}

void Workspace::raiseOrLowerClient(AbstractClient *c)
{
    if (!c) return;
    AbstractClient* topmost = nullptr;
// TODO    Q_ASSERT( block_stacking_updates == 0 );
    if (most_recently_raised && contains(stacking_order, most_recently_raised) &&
            most_recently_raised->isShown(true) && c->isOnCurrentDesktop())
        topmost = most_recently_raised;
    else
        topmost = topClientOnDesktop(c->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : c->desktop(),
                                     options->isSeparateScreenFocus() ? c->screen() : -1);

    if (c == topmost) {
        lowerClient(c);
    } else {
        raiseClient(c);
    }
}


void Workspace::lowerClient(AbstractClient* c, bool nogroup)
{
    if (!c)
        return;

    c->control()->cancel_auto_raise();

    StackingUpdatesBlocker blocker(this);

    remove_all(unconstrained_stacking_order, c);
    unconstrained_stacking_order.push_front(c);

    if (!nogroup && c->isTransient()) {
        // lower also all windows in the group, in their reversed stacking order
        std::deque<X11Client*> wins;
        if (auto group = c->group()) {
            wins = ensureStackingOrder(group->members());
        }
        for (int i = wins.size() - 1;
                i >= 0;
                --i) {
            if (wins[ i ] != c)
                lowerClient(wins[ i ], true);
        }
    }

    if (c == most_recently_raised)
        most_recently_raised = nullptr;
}

void Workspace::lowerClientWithinApplication(AbstractClient* c)
{
    if (!c)
        return;

    c->control()->cancel_auto_raise();

    StackingUpdatesBlocker blocker(this);

    remove_all(unconstrained_stacking_order, c);

    bool lowered = false;
    // first try to put it below the bottom-most window of the application
    for (auto it = unconstrained_stacking_order.begin();
            it != unconstrained_stacking_order.end();
            ++it) {
        AbstractClient *client = qobject_cast<AbstractClient*>(*it);
        if (!client) {
            continue;
        }
        if (win::belong_to_same_client(client, c)) {
            unconstrained_stacking_order.insert(it, c);
            lowered = true;
            break;
        }
    }
    if (!lowered)
        unconstrained_stacking_order.push_front(c);
    // ignore mainwindows
}

void Workspace::raiseClient(AbstractClient* c, bool nogroup)
{
    if (!c) {
        return;
    }

    c->control()->cancel_auto_raise();

    StackingUpdatesBlocker blocker(this);

    if (!nogroup && c->isTransient()) {
        std::vector<AbstractClient*> leads;

        auto lead = c;
        while (true) {
            lead = dynamic_cast<AbstractClient*>(lead->control()->transient_lead());
            if (!lead) {
                break;
            }
            leads.push_back(lead);
        }
        for (auto const& lead : leads) {
            raiseClient(lead, true);
        }
    }

    remove_all(unconstrained_stacking_order, c);
    unconstrained_stacking_order.push_back(c);

    if (!win::is_special_window(c)) {
        most_recently_raised = c;
    }
}

void Workspace::raiseClientWithinApplication(AbstractClient* c)
{
    if (!c)
        return;

    c->control()->cancel_auto_raise();

    StackingUpdatesBlocker blocker(this);
    // ignore mainwindows

    // first try to put it above the top-most window of the application
    for (int i = unconstrained_stacking_order.size() - 1; i > -1 ; --i) {
        auto other = qobject_cast<AbstractClient*>(unconstrained_stacking_order.at(i));
        if (!other) {
            continue;
        }
        if (other == c) {
            // Don't lower it just because it asked to be raised.
            return;
        }
        if (win::belong_to_same_client(other, c)) {
            remove_all(unconstrained_stacking_order, c);
            auto it = find(unconstrained_stacking_order, other);
            assert(it != unconstrained_stacking_order.end());
            // Insert after the found one.
            unconstrained_stacking_order.insert(it + 1, c);
            break;
        }
    }
}

void Workspace::raiseClientRequest(KWin::AbstractClient *c, NET::RequestSource src, xcb_timestamp_t timestamp)
{
    if (src == NET::FromTool || allowFullClientRaising(c, timestamp))
        raiseClient(c);
    else {
        raiseClientWithinApplication(c);
        win::set_demands_attention(c, true);
    }
}

void Workspace::lowerClientRequest(KWin::X11Client *c, NET::RequestSource src, xcb_timestamp_t /*timestamp*/)
{
    // If the client has support for all this focus stealing prevention stuff,
    // do only lowering within the application, as that's the more logical
    // variant of lowering when application requests it.
    // No demanding of attention here of course.
    if (src == NET::FromTool || !c->hasUserTimeSupport())
        lowerClient(c);
    else
        lowerClientWithinApplication(c);
}

void Workspace::lowerClientRequest(KWin::AbstractClient *c)
{
    lowerClientWithinApplication(c);
}

void Workspace::restack(AbstractClient* c, AbstractClient* under, bool force)
{
    assert(contains(unconstrained_stacking_order, under));

    if (!force && !win::belong_to_same_client(under, c)) {
         // put in the stacking order below _all_ windows belonging to the active application
        for (size_t i = 0; i < unconstrained_stacking_order.size(); ++i) {
            auto other = qobject_cast<AbstractClient*>(unconstrained_stacking_order.at(i));
            if (other && other->layer() == c->layer() && win::belong_to_same_client(under, other)) {
                under = (c == other) ? nullptr : other;
                break;
            }
        }
    }
    if (under) {
        remove_all(unconstrained_stacking_order, c);
        auto it = find(unconstrained_stacking_order, under);
        unconstrained_stacking_order.insert(it, c);
    }

    assert(contains(unconstrained_stacking_order, c));
    FocusChain::self()->moveAfterClient(c, under);
    updateStackingOrder();
}

void Workspace::restackClientUnderActive(AbstractClient* c)
{
    if (!active_client || active_client == c || active_client->layer() != c->layer()) {
        raiseClient(c);
        return;
    }
    restack(c, active_client);
}

void Workspace::restoreSessionStackingOrder(X11Client *c)
{
    if (c->sessionStackingOrder() < 0)
        return;

    StackingUpdatesBlocker blocker(this);
    remove_all(unconstrained_stacking_order, c);

    for (auto it = unconstrained_stacking_order.begin();  // from bottom
            it != unconstrained_stacking_order.end();
            ++it) {
        X11Client *current = qobject_cast<X11Client *>(*it);
        if (!current) {
            continue;
        }
        if (current->sessionStackingOrder() > c->sessionStackingOrder()) {
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
        auto c = qobject_cast<X11Client*>(window);

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

    std::deque<Toplevel*> stacking;
    for (auto lay = static_cast<size_t>(win::layer::first); lay < layer_count; ++lay) {
        stacking.insert(stacking.end(), layer[lay].begin(), layer[lay].end());
    }

    // now keep transients above their mainwindows
    // TODO this could(?) use some optimization
    for (int i = stacking.size() - 1; i >= 0;) {
        // Index of the main window for the current transient window.
        int i2 = -1;

        // If the current transient has "child" transients, we'd like to restart
        // construction of the constrained stacking order from the position where
        // the current transient will be moved.
        bool hasTransients = false;

        // Find topmost client this one is transient for.
        if (auto *client = qobject_cast<AbstractClient *>(stacking[i])) {
            if (!client->isTransient()) {
                --i;
                continue;
            }
            for (i2 = stacking.size() - 1; i2 >= 0; --i2) {
                auto *c2 = qobject_cast<AbstractClient *>(stacking[i2]);
                if (!c2) {
                    continue;
                }
                if (c2 == client) {
                    i2 = -1; // Don't reorder, already on top of its main window.
                    break;
                }
                if (c2->control()->has_transient(client, true)
                        && keepTransientAbove(c2, client)) {
                    break;
                }
            }

            hasTransients = !client->control()->transients().empty();

            // If the current transient doesn't have any "alive" transients, check
            // whether it has deleted transients that have to be raised.
            const bool searchForDeletedTransients = !hasTransients
                && !deletedList().empty();
            if (searchForDeletedTransients) {
                for (size_t j = i + 1; j < stacking.size(); ++j) {
                    auto *deleted = qobject_cast<Deleted *>(stacking[j]);
                    if (!deleted) {
                        continue;
                    }
                    if (deleted->wasTransientFor(client)) {
                        hasTransients = true;
                        break;
                    }
                }
            }
        } else if (auto *deleted = qobject_cast<Deleted *>(stacking[i])) {
            if (!deleted->wasTransient()) {
                --i;
                continue;
            }
            for (i2 = stacking.size() - 1; i2 >= 0; --i2) {
                Toplevel *c2 = stacking[i2];
                if (c2 == deleted) {
                    i2 = -1; // Don't reorder, already on top of its main window.
                    break;
                }
                if (deleted->wasTransientFor(c2)
                        && keepDeletedTransientAbove(c2, deleted)) {
                    break;
                }
            }
            hasTransients = !deleted->transients().isEmpty();
        }

        if (i2 == -1) {
            --i;
            continue;
        }

        auto current = stacking[i];
        stacking.erase(stacking.begin() + i);

        --i; // move onto the next item (for next for () iteration)
        --i2; // adjust index of the mainwindow after the remove above
        if (hasTransients) {  // this one now can be possibly above its transients,
            i = i2; // so go again higher in the stack order and possibly move those transients again
        }
        ++i2; // insert after (on top of) the mainwindow, it's ok if it2 is now stacking.end()

        stacking.insert(stacking.begin() + i2, current);
    }
    return stacking;
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
std::deque<X11Client*> Workspace::ensureStackingOrder(std::vector<X11Client*> const& list) const
{
    return ensureStackingOrderInList(stacking_order, list);
}

std::deque<AbstractClient*> Workspace::ensureStackingOrder(std::vector<AbstractClient*> const& list) const
{
    return ensureStackingOrderInList(stacking_order, list);
}

std::deque<AbstractClient*> Workspace::ensureStackingOrder(std::vector<Toplevel*> const& list) const
{
    return ensureStackingOrderInList<Toplevel, AbstractClient>(stacking_order, list);
}

// check whether a transient should be actually kept above its mainwindow
// there may be some special cases where this rule shouldn't be enfored
bool Workspace::keepTransientAbove(const AbstractClient* mainwindow, const AbstractClient* transient)
{
    // #93832 - don't keep splashscreens above dialogs
    if (win::is_splash(transient) && win::is_dialog(mainwindow))
        return false;
    // This is rather a hack for #76026. Don't keep non-modal dialogs above
    // the mainwindow, but only if they're group transient (since only such dialogs
    // have taskbar entry in Kicker). A proper way of doing this (both kwin and kicker)
    // needs to be found.
    if (win::is_dialog(transient) && !transient->control()->modal() && transient->groupTransient())
        return false;
    // #63223 - don't keep transients above docks, because the dock is kept high,
    // and e.g. dialogs for them would be too high too
    // ignore this if the transient has a placement hint which indicates it should go above it's parent
    if (win::is_dock(mainwindow) && !transient->hasTransientPlacementHint())
        return false;
    return true;
}

bool Workspace::keepDeletedTransientAbove(const Toplevel *mainWindow, const Deleted *transient) const
{
    // #93832 - Don't keep splashscreens above dialogs.
    if (win::is_splash(transient) && win::is_dialog(mainWindow)) {
        return false;
    }

    if (transient->wasX11Client()) {
        // If a group transient was active, we should keep it above no matter
        // what, because at the time when the transient was closed, it was above
        // the main window.
        if (transient->wasGroupTransient() && transient->wasActive()) {
            return true;
        }

        // This is rather a hack for #76026. Don't keep non-modal dialogs above
        // the mainwindow, but only if they're group transient (since only such
        // dialogs have taskbar entry in Kicker). A proper way of doing this
        // (both kwin and kicker) needs to be found.
        if (transient->wasGroupTransient() && win::is_dialog(transient)
                && !transient->control()->modal()) {
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
    foreach (Toplevel * c, stacking_order)
    x_stacking.push_back(c);

    if (tree && !tree->isNull()) {
        xcb_window_t *windows = tree->children();
        const auto count = tree->data()->children_len;
        auto foundUnmanagedCount = unmanaged.size();
        for (size_t i = 0; i < count; ++i) {
            for (auto const& u : unmanaged) {
                if (u->window() == windows[i]) {
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

    for (InternalClient *client : workspace()->internalClients()) {
        if (client->isShown(false)) {
            x_stacking.push_back(client);
        }
    }

    m_xStackingDirty = false;
}

//*******************************
// Client
//*******************************

void X11Client::restackWindow(xcb_window_t above, int detail, NET::RequestSource src, xcb_timestamp_t timestamp, bool send_event)
{
    X11Client *other = nullptr;
    if (detail == XCB_STACK_MODE_OPPOSITE) {
        other = workspace()->findClient(Predicate::WindowMatch, above);
        if (!other) {
            workspace()->raiseOrLowerClient(this);
            return;
        }

        auto it = workspace()->stackingOrder().cbegin();
        auto end = workspace()->stackingOrder().cend();

        while (it != end) {
            if (*it == this) {
                detail = XCB_STACK_MODE_ABOVE;
                break;
            } else if (*it == other) {
                detail = XCB_STACK_MODE_BELOW;
                break;
            }
            ++it;
        }
    }
    else if (detail == XCB_STACK_MODE_TOP_IF) {
        other = workspace()->findClient(Predicate::WindowMatch, above);
        if (other && other->frameGeometry().intersects(frameGeometry()))
            workspace()->raiseClientRequest(this, src, timestamp);
        return;
    }
    else if (detail == XCB_STACK_MODE_BOTTOM_IF) {
        other = workspace()->findClient(Predicate::WindowMatch, above);
        if (other && other->frameGeometry().intersects(frameGeometry()))
            workspace()->lowerClientRequest(this, src, timestamp);
        return;
    }

    if (!other)
        other = workspace()->findClient(Predicate::WindowMatch, above);

    if (other && detail == XCB_STACK_MODE_ABOVE) {
        auto it = workspace()->stackingOrder().cend();
        auto begin = workspace()->stackingOrder().cbegin();

        while (--it != begin) {
            if (*it == other) {
                // the other one is top on stack
                // invalidate and force
                it = begin;
                src = NET::FromTool;
                break;
            }
            X11Client *c = qobject_cast<X11Client *>(*it);

            if (!c || !(  win::is_normal(*it) && c->isShown(true) &&
                    (*it)->isOnCurrentDesktop() && (*it)->isOnCurrentActivity() && win::on_screen(*it, screen()) ))
                continue; // irrelevant clients

            if (*(it - 1) == other)
                break; // "it" is the one above the target one, stack below "it"
        }

        if (it != begin && (*(it - 1) == other))
            other = qobject_cast<X11Client *>(*it);
        else
            other = nullptr;
    }

    if (other)
        workspace()->restack(this, other);
    else if (detail == XCB_STACK_MODE_BELOW)
        workspace()->lowerClientRequest(this, src, timestamp);
    else if (detail == XCB_STACK_MODE_ABOVE)
        workspace()->raiseClientRequest(this, src, timestamp);

    if (send_event)
        sendSyntheticConfigureNotify();
}

void X11Client::doSetKeepAbove()
{
}

void X11Client::doSetKeepBelow()
{
}

bool X11Client::belongsToDesktop() const
{
    foreach (const X11Client *c, group()->members()) {
        if (win::is_desktop(c)) {
            return true;
        }
    }
    return false;
}

} // namespace
