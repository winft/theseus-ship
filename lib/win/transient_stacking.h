/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net.h"
#include "transient.h"

namespace KWin::win
{

// check whether a transient should be actually kept above its mainwindow
// there may be some special cases where this rule shouldn't be enfored
template<typename Win1, typename Win2>
bool keep_transient_above(Win1 const* mainwindow, Win2 const* child)
{
    if (child->transient->annexed) {
        return true;
    }

    // #93832 - don't keep splashscreens above dialogs
    if (win::is_splash(child) && win::is_dialog(mainwindow)) {
        return false;
    }

    // This is rather a hack for #76026. Don't keep non-modal dialogs above
    // the mainwindow, but only if they're group transient (since only such dialogs
    // have taskbar entry in Kicker). A proper way of doing this (both kwin and kicker)
    // needs to be found.
    if (win::is_dialog(child) && !child->transient->modal() && is_group_transient(*child)) {
        return false;
    }

    // #63223 - don't keep transients above docks, because the dock is kept high,
    // and e.g. dialogs for them would be too high too
    // ignore this if the transient has a placement hint which indicates it should go above it's
    // parent
    if (win::is_dock(mainwindow)) {
        return false;
    }
    return true;
}

template<typename Win1, typename Win2>
bool keep_deleted_transient_above(Win1 const* mainWindow, Win2 const* child)
{
    assert(child->remnant);

    // #93832 - Don't keep splashscreens above dialogs.
    if (win::is_splash(child) && win::is_dialog(mainWindow)) {
        return false;
    }

    if (child->remnant->data.was_x11_client) {
        // If a group transient was active, we should keep it above no matter
        // what, because at the time when the transient was closed, it was above
        // the main window.
        if (child->remnant->data.was_group_transient && child->remnant->data.was_active) {
            return true;
        }

        // This is rather a hack for #76026. Don't keep non-modal dialogs above
        // the mainwindow, but only if they're group transient (since only such
        // dialogs have taskbar entry in Kicker). A proper way of doing this
        // (both kwin and kicker) needs to be found.
        if (child->remnant->data.was_group_transient && win::is_dialog(child)
            && !child->transient->modal()) {
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
