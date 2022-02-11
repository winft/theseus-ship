/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "controlling.h"
#include "focus_chain.h"
#include "geo.h"
#include "layers.h"
#include "meta.h"
#include "net.h"
#include "stacking_order.h"
#include "transient.h"
#include "utils/blocker.h"
#include "win/util.h"
#include "x11/group.h"

// Required for casts between Toplevel and window in some template functions.
// TODO(romangg): Remove these casts and this include to make the functions truly generic.
#include "x11/window.h"

#include "rules/rules.h"
#include "workspace.h"

/**
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
 The NoficationLayer contains notification windows which are kept above all windows except the
 active fullscreen window. The CriticalNotificationLayer contains notification windows which are
 important enough to keep them even above fullscreen windows. The OnScreenDisplayLayer is used for
 eg. volume and brightness change feedback and is kept above all windows since it provides immediate
 response to a user action.

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

namespace KWin::win
{

/**
 * Returns topmost visible client. Windows on the dock, the desktop
 * or of any other special kind are excluded. Also if the window
 * doesn't accept focus it's excluded.
 */
// TODO misleading name for this method, too many slightly different ways to use it
template<typename Space>
Toplevel* top_client_on_desktop(Space* space,
                                int desktop,
                                int screen,
                                bool unconstrained = false,
                                bool only_normal = true)
{
    // TODO    Q_ASSERT( block_stacking_updates == 0 );
    const auto& list
        = unconstrained ? space->stacking_order->pre_stack : space->stacking_order->sorted();
    for (auto it = std::crbegin(list); it != std::crend(list); it++) {
        auto c = *it;
        if (c && c->isOnDesktop(desktop) && c->isShown()) {
            if (screen != -1 && c->screen() != screen)
                continue;
            if (!only_normal)
                return c;
            if (wants_tab_focus(c) && !is_special_window(c))
                return c;
        }
    }
    return nullptr;
}

template<typename Space>
Toplevel* find_desktop(Space* space, bool topmost, int desktop)
{
    // TODO(fsorr): use C++20 std::ranges::reverse_view
    auto const& list = space->stacking_order->sorted();
    auto is_desktop = [desktop](auto window) {
        return window->control && window->isOnDesktop(desktop) && win::is_desktop(window)
            && window->isShown();
    };

    if (topmost) {
        auto it = std::find_if(list.rbegin(), list.rend(), is_desktop);
        if (it != list.rend()) {
            return *it;
        }
    } else {
        // bottom-most
        auto it = std::find_if(list.begin(), list.end(), is_desktop);
        if (it != list.end()) {
            return *it;
        }
    }

    return nullptr;
}

template<class T, class R = T>
std::deque<R*> ensure_stacking_order_in_list(std::deque<Toplevel*> const& stackingOrder,
                                             std::vector<T*> const& list)
{
    static_assert(std::is_base_of<Toplevel, T>::value, "U must be derived from T");
    // TODO    Q_ASSERT( block_stacking_updates == 0 );

    if (!list.size()) {
        return std::deque<R*>();
    }
    if (list.size() < 2) {
        return std::deque<R*>({qobject_cast<R*>(list.at(0))});
    }

    // TODO is this worth optimizing?
    std::deque<R*> result;
    for (auto win : list) {
        if (auto rwin = qobject_cast<R*>(win)) {
            result.push_back(rwin);
        }
    }

    // Now reorder the result. For that stackingOrder should be a superset and it define the order
    // in which windows should appear in result. We then reorder result simply by going through
    // stackingOrder one-by-one, removing it from result and then adding it back in the end.
    for (auto win : stackingOrder) {
        auto rwin = qobject_cast<R*>(win);
        if (!rwin) {
            continue;
        }
        if (contains(result, rwin)) {
            remove_all(result, rwin);
            result.push_back(rwin);
        }
    }

    return result;
}

template<class Space, class Win>
std::deque<Win*> restacked_by_space_stacking_order(Space* space, std::vector<Win*> const& list)
{
    return ensure_stacking_order_in_list(space->stacking_order->sorted(), list);
}

template<typename Space, typename Window>
void lower_window(Space* space, Window* window)
{
    assert(window->control);

    auto do_lower = [space](auto win) {
        win->control->cancel_auto_raise();

        blocker block(space->stacking_order);

        remove_all(space->stacking_order->pre_stack, win);
        space->stacking_order->pre_stack.push_front(win);

        return block;
    };
    auto cleanup = [space](auto win) {
        if (win == space->most_recently_raised) {
            space->most_recently_raised = nullptr;
        }
    };

    auto block = do_lower(window);

    if (window->transient()->lead() && window->group()) {
        // Lower also all windows in the group, in reversed stacking order.
        auto const wins = restacked_by_space_stacking_order(space, window->group()->members());

        for (auto it = wins.crbegin(); it != wins.crend(); it++) {
            auto gwin = *it;
            if (gwin == static_cast<Toplevel*>(window)) {
                continue;
            }

            assert(gwin->control);
            do_lower(gwin);
            cleanup(gwin);
        }
    }

    cleanup(window);
}

template<typename Space, typename Window>
void raise_window(Space* space, Window* window)
{
    if (!window) {
        return;
    }

    auto prepare = [space](auto window) {
        assert(window->control);
        window->control->cancel_auto_raise();
        return blocker(space->stacking_order);
    };
    auto do_raise = [space](auto window) {
        remove_all(space->stacking_order->pre_stack, window);
        space->stacking_order->pre_stack.push_back(window);

        if (!is_special_window(window)) {
            space->most_recently_raised = static_cast<Toplevel*>(window);
        }
    };

    auto block = prepare(window);

    if (window->transient()->lead()) {
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

        auto stacked_leads = restacked_by_space_stacking_order(space, leads);

        for (auto lead : stacked_leads) {
            if (!lead->control) {
                // Might be without control, at least on X11 this can happen (latte-dock settings).
                continue;
            }
            auto block = prepare(lead);
            do_raise(lead);
        }
    }

    do_raise(window);
}

template<typename Space, typename Window>
void raise_or_lower_client(Space* space, Window* window)
{
    if (!window) {
        return;
    }

    Toplevel* topmost = nullptr;

    if (space->most_recently_raised
        && contains(space->stacking_order->sorted(), space->most_recently_raised)
        && space->most_recently_raised->isShown() && window->isOnCurrentDesktop()) {
        topmost = space->most_recently_raised;
    } else {
        topmost = top_client_on_desktop(space,
                                        window->isOnAllDesktops()
                                            ? win::virtual_desktop_manager::self()->current()
                                            : window->desktop(),
                                        options->isSeparateScreenFocus() ? window->screen() : -1);
    }

    if (window == topmost) {
        lower_window(space, window);
    } else {
        raise_window(space, window);
    }
}

template<typename Space, typename Window>
void restack(Space* space, Window* window, Toplevel* under, bool force = false)
{
    assert(contains(space->stacking_order->pre_stack, under));

    if (!force && !belong_to_same_client(under, window)) {
        // put in the stacking order below _all_ windows belonging to the active application
        for (auto it = space->stacking_order->pre_stack.crbegin();
             it != space->stacking_order->pre_stack.crend();
             it++) {
            auto other = *it;
            if (other->control && other->layer() == window->layer()
                && belong_to_same_client(under, other)) {
                under = (window == other) ? nullptr : other;
                break;
            }
        }
    }
    if (under) {
        remove_all(space->stacking_order->pre_stack, window);
        auto it = find(space->stacking_order->pre_stack, under);
        space->stacking_order->pre_stack.insert(it, window);
    }

    assert(contains(space->stacking_order->pre_stack, window));
    focus_chain::self()->moveAfterClient(window, under);
    space->stacking_order->update();
}

template<typename Space, typename Win>
void restack_client_under_active(Space* space, Win* window)
{
    if (!space->active_client || space->active_client == window
        || space->active_client->layer() != window->layer()) {
        raise_window(space, window);
        return;
    }
    restack(space, window, space->active_client);
}

template<typename Win>
void auto_raise(Win* win)
{
    raise_window(workspace(), win);
    win->control->cancel_auto_raise();
}

template<typename Win>
void set_keep_below(Win* win, bool keep);

template<typename Win>
void set_keep_above(Win* win, bool keep)
{
    keep = win->control->rules().checkKeepAbove(keep);
    if (keep && !win->control->rules().checkKeepBelow(false)) {
        set_keep_below(win, false);
    }
    if (keep == win->control->keep_above()) {
        // force hint change if different
        if (win->info && bool(win->info->state() & NET::KeepAbove) != keep) {
            win->info->setState(keep ? NET::KeepAbove : NET::States(), NET::KeepAbove);
        }
        return;
    }
    win->control->set_keep_above(keep);
    if (win->info) {
        win->info->setState(keep ? NET::KeepAbove : NET::States(), NET::KeepAbove);
    }
    update_layer(win);
    win->updateWindowRules(Rules::Above);

    win->doSetKeepAbove();
    Q_EMIT win->keepAboveChanged(keep);
}

template<typename Win>
void set_keep_below(Win* win, bool keep)
{
    keep = win->control->rules().checkKeepBelow(keep);
    if (keep && !win->control->rules().checkKeepAbove(false)) {
        set_keep_above(win, false);
    }
    if (keep == win->control->keep_below()) {
        // force hint change if different
        if (win->info && bool(win->info->state() & NET::KeepBelow) != keep)
            win->info->setState(keep ? NET::KeepBelow : NET::States(), NET::KeepBelow);
        return;
    }
    win->control->set_keep_below(keep);
    if (win->info) {
        win->info->setState(keep ? NET::KeepBelow : NET::States(), NET::KeepBelow);
    }
    update_layer(win);
    win->updateWindowRules(Rules::Below);

    win->doSetKeepBelow();
    Q_EMIT win->keepBelowChanged(keep);
}

/**
 * Sets the client's active state to \a act.
 *
 * This function does only change the visual appearance of the client,
 * it does not change the focus setting. Use
 * Workspace::activateClient() or Workspace::requestFocus() instead.
 *
 * If a client receives or looses the focus, it calls setActive() on
 * its own.
 */
template<typename Win>
void set_active(Win* win, bool active)
{
    if (win->control->active() == active) {
        return;
    }
    win->control->set_active(active);

    auto const ruledOpacity = active
        ? win->control->rules().checkOpacityActive(qRound(win->opacity() * 100.0))
        : win->control->rules().checkOpacityInactive(qRound(win->opacity() * 100.0));
    win->setOpacity(ruledOpacity / 100.0);

    workspace()->setActiveClient(active ? win : nullptr);

    if (!active) {
        win->control->cancel_auto_raise();
    }

    blocker block(workspace()->stacking_order);

    // active windows may get different layer
    update_layer(win);

    auto leads = win->transient()->leads();
    for (auto lead : leads) {
        if (lead->remnant()) {
            continue;
        }
        if (lead->control->fullscreen()) {
            // Fullscreens go high even if their transient is active.
            update_layer(lead);
        }
    }

    win->doSetActive();
    Q_EMIT win->activeChanged();
    win->control->update_mouse_grab();
}

template<typename Win>
void set_demands_attention(Win* win, bool demand)
{
    if (win->control->active()) {
        demand = false;
    }
    if (win->control->demands_attention() == demand) {
        return;
    }
    win->control->set_demands_attention(demand);

    if (win->info) {
        win->info->setState(demand ? NET::DemandsAttention : NET::States(), NET::DemandsAttention);
    }

    workspace()->clientAttentionChanged(win, demand);
    Q_EMIT win->demandsAttentionChanged();
}

template<typename Win>
void set_minimized(Win* win, bool set, bool avoid_animation = false)
{
    if (set) {
        if (!win->isMinimizable() || win->control->minimized())
            return;

        win->control->set_minimized(true);
        win->doMinimize();

        win->updateWindowRules(Rules::Minimize);
        // TODO: merge signal with s_minimized
        win->addWorkspaceRepaint(visible_rect(win));
        Q_EMIT win->clientMinimized(win, !avoid_animation);
        Q_EMIT win->minimizedChanged();
    } else {
        if (!win->control->minimized()) {
            return;
        }
        if (win->control->rules().checkMinimize(false)) {
            return;
        }

        win->control->set_minimized(false);
        win->doMinimize();

        win->updateWindowRules(Rules::Minimize);
        Q_EMIT win->clientUnminimized(win, !avoid_animation);
        Q_EMIT win->minimizedChanged();
    }
}

// check whether a transient should be actually kept above its mainwindow
// there may be some special cases where this rule shouldn't be enfored
template<typename Win1, typename Win2>
bool keep_transient_above(Win1 const* mainwindow, Win2 const* transient)
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
    if (win::is_dialog(transient) && !transient->transient()->modal()
        && transient->groupTransient())
        return false;
    // #63223 - don't keep transients above docks, because the dock is kept high,
    // and e.g. dialogs for them would be too high too
    // ignore this if the transient has a placement hint which indicates it should go above it's
    // parent
    if (win::is_dock(mainwindow))
        return false;
    return true;
}

template<typename Win1, typename Win2>
bool keep_deleted_transient_above(Win1 const* mainWindow, Win2 const* transient)
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

}
