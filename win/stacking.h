/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "geo.h"
#include "layers.h"
#include "net.h"
#include "transient.h"

#include "rules/rules.h"
#include "workspace.h"

namespace KWin::win
{

template<typename Win>
bool is_active_fullscreen(Win const* win)
{
    if (!win->control->fullscreen()) {
        return false;
    }

    // Instead of activeClient() - avoids flicker.
    auto const ac = workspace()->mostRecentlyActivatedClient();

    // According to NETWM spec implementation notes suggests "focused windows having state
    // _NET_WM_STATE_FULLSCREEN" to be on the highest layer. Also take the screen into account.
    return ac
        && (ac == win || ac->screen() != win->screen() || contains(ac->transient()->leads(), win));
}

template<typename Win>
layer belong_to_layer(Win* win)
{
    // NOTICE while showingDesktop, desktops move to the AboveLayer
    // (interchangeable w/ eg. yakuake etc. which will at first remain visible)
    // and the docks move into the NotificationLayer (which is between Above- and
    // ActiveLayer, so that active fullscreen windows will still cover everything)
    // Since the desktop is also activated, nothing should be in the ActiveLayer, though
    if (win->isInternal()) {
        return win::layer::unmanaged;
    }
    if (win->isLockScreen()) {
        return win::layer::unmanaged;
    }
    if (is_desktop(win)) {
        return workspace()->showingDesktop() ? win::layer::above : win::layer::desktop;
    }
    if (is_splash(win)) {
        return win::layer::normal;
    }
    if (is_dock(win)) {
        if (workspace()->showingDesktop()) {
            return win::layer::notification;
        }
        return win->layer_for_dock();
    }
    if (is_on_screen_display(win)) {
        return win::layer::on_screen_display;
    }
    if (is_notification(win)) {
        return win::layer::notification;
    }
    if (is_critical_notification(win)) {
        return win::layer::critical_notification;
    }
    if (workspace()->showingDesktop() && win->belongsToDesktop()) {
        return win::layer::above;
    }
    if (win->control->keep_below()) {
        return win::layer::below;
    }
    if (is_active_fullscreen(win)) {
        return win::layer::active;
    }
    if (win->control->keep_above()) {
        return win::layer::above;
    }
    return win::layer::normal;
}

template<typename Win>
void invalidate_layer(Win* win)
{
    win->set_layer(win::layer::unknown);
}

template<typename Win>
void update_layer(Win* win)
{
    if (!win) {
        return;
    }
    if (win->remnant() || win->layer() == belong_to_layer(win)) {
        return;
    }
    StackingUpdatesBlocker blocker(workspace());

    // Invalidate, will be updated when doing restacking.
    invalidate_layer(win);

    for (auto const& child : win->transient()->children) {
        if (!child->transient()->annexed) {
            update_layer(child);
        }
    }
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

    StackingUpdatesBlocker blocker(workspace());

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
    for (auto c : list) {
        result.push_back(qobject_cast<R*>(c));
    }
    for (auto it = stackingOrder.begin(); it != stackingOrder.end(); ++it) {
        R* c = qobject_cast<R*>(*it);
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
