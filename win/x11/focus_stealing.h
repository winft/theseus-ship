/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/options.h"
#include "main.h"
#include "utils/algorithm.h"
#include "win/desktop_get.h"
#include "win/layers.h"
#include "win/util.h"

#include <xcb/xcb.h>

namespace KWin::win::x11
{

// focus_in -> the window got FocusIn event
// ignore_desktop - call comes from _NET_ACTIVE_WINDOW message, don't refuse just because of window
//     is on a different desktop
template<typename Space, typename Win>
bool allow_window_activation(Space& space,
                             Win const* window,
                             xcb_timestamp_t time = -1U,
                             bool focus_in = false,
                             bool ignore_desktop = false)
{
    // kwinApp()->options->focusStealingPreventionLevel :
    // 0 - none    - old KWin behaviour, new windows always get focus
    // 1 - low     - focus stealing prevention is applied normally, when unsure, activation is
    // allowed 2 - normal  - focus stealing prevention is applied normally, when unsure, activation
    // is not allowed,
    //              this is the default
    // 3 - high    - new window gets focus only if it belongs to the active application,
    //              or when no window is currently active
    // 4 - extreme - no window gets focus without user intervention
    if (time == -1U) {
        time = window->userTime();
    }

    auto level = window->control->rules.checkFSP(
        kwinApp()->options->qobject->focusStealingPreventionLevel());
    if (space.session_manager->state() == SessionState::Saving
        && enum_index(level) <= enum_index(fsp_level::medium)) {
        // <= normal
        return true;
    }

    auto ac = most_recently_activated_window(space);
    if (focus_in) {
        if (std::find(space.stacking.should_get_focus.cbegin(),
                      space.stacking.should_get_focus.cend(),
                      const_cast<Win*>(window))
            != space.stacking.should_get_focus.cend()) {
            // FocusIn was result of KWin's action
            return true;
        }
        // Before getting FocusIn, the active Client already
        // got FocusOut, and therefore got deactivated.
        ac = space.stacking.last_active;
    }
    if (time == 0) {
        // explicitly asked not to get focus
        if (!window->control->rules.checkAcceptFocus(false))
            return false;
    }

    auto const protection = ac ? ac->control->rules.checkFPP(fsp_level::medium) : fsp_level::none;

    // stealing is unconditionally allowed (NETWM behavior)
    if (level == fsp_level::none || protection == fsp_level::none) {
        return true;
    }

    // The active client "grabs" the focus or stealing is generally forbidden
    if (level == fsp_level::extreme || protection == fsp_level::extreme) {
        return false;
    }

    // Desktop switching is only allowed in the "no protection" case
    if (!ignore_desktop && !on_current_desktop(window)) {
        // allow only with level == 0
        return false;
    }

    // No active client, it's ok to pass focus
    // NOTICE that extreme protection needs to be handled before to allow protection on unmanged
    // windows
    if (!ac || is_desktop(ac)) {
        qCDebug(KWIN_CORE) << "Activation: No client active, allowing";
        // no active client -> always allow
        return true;
    }

    // TODO window urgency  -> return true?

    // Unconditionally allow intra-client passing around for lower stealing protections
    // unless the active client has High interest
    if (belong_to_same_client(window, ac, same_client_check::relaxed_for_active)
        && protection < fsp_level::high) {
        qCDebug(KWIN_CORE) << "Activation: Belongs to active application";
        return true;
    }

    if (!on_current_desktop(window)) {
        // we allowed explicit self-activation across virtual desktops
        // inside a client or if no client was active, but not otherwise
        return false;
    }

    // High FPS, not intr-client change. Only allow if the active client has only minor interest
    if (level > fsp_level::medium && protection > fsp_level::low) {
        return false;
    }

    if (time == -1U) { // no time known
        qCDebug(KWIN_CORE) << "Activation: No timestamp at all";
        // Only allow for Low protection unless active client has High interest in focus
        if (level < fsp_level::medium && protection < fsp_level::high) {
            return true;
        }

        // no timestamp at all, don't activate - because there's also creation timestamp
        // done on CreateNotify, this case should happen only in case application
        // maps again already used window, i.e. this won't happen after app startup
        return false;
    }

    // Low or medium FSP level, usertime comparism is possible
    xcb_timestamp_t const user_time = ac->userTime();
    qCDebug(KWIN_CORE) << "Activation, compared:" << window << ":" << time << ":" << user_time
                       << ":" << (NET::timestampCompare(time, user_time) >= 0);

    // time >= user_time
    return NET::timestampCompare(time, user_time) >= 0;
}

// basically the same like allowClientActivation(), this time allowing
// a window to be fully raised upon its own request (XRaiseWindow),
// if refused, it will be raised only on top of windows belonging
// to the same application
template<typename Space, typename Win>
bool allow_full_window_raising(Space& space, Win const* window, xcb_timestamp_t time)
{
    auto level = window->control->rules.checkFSP(
        kwinApp()->options->qobject->focusStealingPreventionLevel());
    if (space.session_manager->state() == SessionState::Saving
        && enum_index(level) <= enum_index(fsp_level::medium)) {
        // <= normal
        return true;
    }

    auto ac = most_recently_activated_window(space);

    if (level == fsp_level::none) {
        return true;
    }
    if (level == fsp_level::extreme) {
        return false;
    }

    if (!ac || is_desktop(ac)) {
        qCDebug(KWIN_CORE) << "Raising: No client active, allowing";
        // no active client -> always allow
        return true;
    }

    // TODO window urgency  -> return true?
    if (belong_to_same_client(window, ac, same_client_check::relaxed_for_active)) {
        qCDebug(KWIN_CORE) << "Raising: Belongs to active application";
        return true;
    }

    if (level == fsp_level::high) {
        return false;
    }

    xcb_timestamp_t user_time = ac->userTime();
    qCDebug(KWIN_CORE) << "Raising, compared:" << time << ":" << user_time << ":"
                       << (NET::timestampCompare(time, user_time) >= 0);

    // time >= user_time
    return NET::timestampCompare(time, user_time) >= 0;
}

}
