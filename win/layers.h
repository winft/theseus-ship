/*
    SPDX-FileCopyrightText: ...

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net.h"
#include "space.h"
#include "transient.h"

#include "toplevel.h"
#include "utils/blocker.h"

namespace KWin
{
class Toplevel;

namespace win
{

template<typename Win>
bool is_active_fullscreen(Win const* win)
{
    if (!win->control->fullscreen()) {
        return false;
    }

    // Instead of activeClient() - avoids flicker.
    auto const ac = win->space.mostRecentlyActivatedClient();

    // According to NETWM spec implementation notes suggests "focused windows having state
    // _NET_WM_STATE_FULLSCREEN" to be on the highest layer. Also take the screen into account.
    return ac
        && (ac == win || ac->central_output != win->central_output
            || contains(ac->transient()->leads(), win));
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
        return win->space.showingDesktop() ? win::layer::above : win::layer::desktop;
    }
    if (is_splash(win)) {
        return win::layer::normal;
    }
    if (is_dock(win)) {
        if (win->space.showingDesktop()) {
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
    if (win->space.showingDesktop() && win->belongsToDesktop()) {
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

    blocker block(win->space.stacking_order);

    // Invalidate, will be updated when doing restacking.
    invalidate_layer(win);

    for (auto const& child : win->transient()->children) {
        if (!child->transient()->annexed) {
            update_layer(child);
        }
    }
}

}
}
