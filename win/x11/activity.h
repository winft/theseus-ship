/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "hide.h"
#include "input.h"
#include "window.h"

#include "activities.h"
#include "atoms.h"
#include "win/focuschain.h"

namespace KWin::win::x11
{

/**
 * Returns the list of activities the client window is on.
 * if it's on all activities, the list will be empty.
 * Don't use this, use isOnActivity() and friends (from class Toplevel)
 */
template<typename Win>
QStringList activities(Win* win)
{
    if (win->session_activity_override) {
        return QStringList();
    }
    return win->activity_list;
}

/**
 * update after activities changed
 */
template<typename Win>
void update_activities(Win* win, bool includeTransients)
{
    if (win->activity_updates_blocked) {
        win->blocked_activity_updates_require_transients |= includeTransients;
        return;
    }

    Q_EMIT win->activitiesChanged(win);

    // reset
    win->blocked_activity_updates_require_transients = false;

    focus_chain::self()->update(win, focus_chain::MakeFirst);
    update_visibility(win);
    win->updateWindowRules(Rules::Activity);
}

/**
 * set exactly which activities this client is on
 */
template<typename Win>
void set_on_activities([[maybe_unused]] Win* win, [[maybe_unused]] QStringList newActivitiesList)
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return;
    }
    QString joinedActivitiesList = newActivitiesList.join(QStringLiteral(","));
    joinedActivitiesList = win->control->rules().checkActivity(joinedActivitiesList, false);
    newActivitiesList = joinedActivitiesList.split(u',', Qt::SkipEmptyParts);

    QStringList allActivities = Activities::self()->all();

    auto it = newActivitiesList.begin();
    while (it != newActivitiesList.end()) {
        if (!allActivities.contains(*it)) {
            it = newActivitiesList.erase(it);
        } else {
            it++;
        }
    }

    if ( // If we got the request to be on all activities explicitly
        newActivitiesList.isEmpty() || joinedActivitiesList == Activities::nullUuid() ||
        // If we got a list of activities that covers all activities
        (newActivitiesList.count() > 1 && newActivitiesList.count() == allActivities.count())) {

        win->activity_list.clear();
        const QByteArray nullUuid = Activities::nullUuid().toUtf8();
        win->xcb_windows.client.changeProperty(
            atoms->activities, XCB_ATOM_STRING, 8, nullUuid.length(), nullUuid.constData());

    } else {
        QByteArray joined = joinedActivitiesList.toLatin1();
        win->activity_list = newActivitiesList;
        win->xcb_windows.client.changeProperty(
            atoms->activities, XCB_ATOM_STRING, 8, joined.length(), joined.constData());
    }

    update_activities(win, false);
#endif
}

/**
 * Sets whether the client is on @p activity.
 * If you remove it from its last activity, then it's on all activities.
 *
 * Note: If it was on all activities and you try to remove it from one, nothing will happen;
 * I don't think that's an important enough use case to handle here.
 */
template<typename Win>
void set_on_activity([[maybe_unused]] Win* win,
                     [[maybe_unused]] QString const& activity,
                     [[maybe_unused]] bool enable)
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return;
    }
    auto newActivitiesList = activities(win);
    if (newActivitiesList.contains(activity) == enable) {
        // nothing to do
        return;
    }
    if (enable) {
        QStringList allActivities = Activities::self()->all();
        if (!allActivities.contains(activity)) {
            // bogus ID
            return;
        }
        newActivitiesList.append(activity);
    } else
        newActivitiesList.removeOne(activity);
    set_on_activities(win, newActivitiesList);
#endif
}

template<typename Win>
void block_activity_updates(Win* win, bool b)
{
    if (b) {
        ++win->activity_updates_blocked;
    } else {
        assert(win->activity_updates_blocked);
        --win->activity_updates_blocked;
        if (!win->activity_updates_blocked)
            update_activities(win, win->blocked_activity_updates_require_transients);
    }
}

/**
 * if @p on is true, sets on all activities.
 * if it's false, sets it to only be on the current activity
 */
template<typename Win>
void set_on_all_activities([[maybe_unused]] Win* win, [[maybe_unused]] bool on)
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (on == win->isOnAllActivities()) {
        return;
    }
    if (on) {
        set_on_activities(win, QStringList());

    } else {
        set_on_activity(win, Activities::self()->current(), true);
    }
#endif
}

template<typename Win>
Xcb::StringProperty fetch_activities(Win* win)
{
#ifdef KWIN_BUILD_ACTIVITIES
    return Xcb::StringProperty(win->xcb_window(), atoms->activities);
#else
    return Xcb::StringProperty();
#endif
}

template<typename Win>
void read_activities(Win* win, Xcb::StringProperty& property)
{
#ifdef KWIN_BUILD_ACTIVITIES
    QStringList newActivitiesList;
    QString prop = QString::fromUtf8(property);
    win->activities_defined = !prop.isEmpty();

    if (prop == Activities::nullUuid()) {
        // copied from setOnAllActivities to avoid a redundant XChangeProperty.
        if (!win->activity_list.isEmpty()) {
            win->activity_list.clear();
            update_activities(win, true);
        }
        return;
    }
    if (prop.isEmpty()) {
        // note: this makes it *act* like it's on all activities but doesn't set the property to
        // 'ALL'
        if (!win->activity_list.isEmpty()) {
            win->activity_list.clear();
            update_activities(win, true);
        }
        return;
    }

    newActivitiesList = prop.split(u',');

    if (newActivitiesList == win->activity_list)
        return; // expected change, it's ok.

    // otherwise, somebody else changed it. we need to validate before reacting.
    // if the activities are not synced, and there are existing clients with
    // activities specified, somebody has restarted kwin. we can not validate
    // activities in this case. we need to trust the old values.
    if (Activities::self()
        && Activities::self()->serviceStatus() != KActivities::Consumer::Unknown) {
        QStringList allActivities = Activities::self()->all();
        if (allActivities.isEmpty()) {
            qCDebug(KWIN_CORE) << "no activities!?!?";
            // don't touch anything, there's probably something bad going on and we don't wanna make
            // it worse
            return;
        }

        for (int i = 0; i < newActivitiesList.size(); ++i) {
            if (!allActivities.contains(newActivitiesList.at(i))) {
                qCDebug(KWIN_CORE) << "invalid:" << newActivitiesList.at(i);
                newActivitiesList.removeAt(i--);
            }
        }
    }
    set_on_activities(win, newActivitiesList);
#endif
}

template<typename Win>
void check_activities(Win* win)
{
#ifdef KWIN_BUILD_ACTIVITIES
    Xcb::StringProperty property = fetch_activities(win);
    read_activities(win, property);
#endif
}

template<typename Win>
void set_session_activity_override(Win* win, bool needed)
{
    win->session_activity_override = needed;
    update_activities(win, false);
}

}
