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
#include "group.h"

#include "startup_info.h"
#include "window.h"
#include "window_find.h"

#include "main.h"
#include "render/effects.h"
#include "win/control.h"
#include "win/space.h"

#include <KStartupInfo>
#include <KWindowSystem>

namespace KWin::win::x11
{

//********************************************
// group
//********************************************

group::group(xcb_window_t xcb_leader, win::space& space)
    : xcb_leader{xcb_leader}
    , space{space}
{
    if (xcb_leader != XCB_WINDOW_NONE) {
        leader
            = find_controlled_window<win::x11::window>(space, predicate_match::window, xcb_leader);
        leader_info = new NETWinInfo(
            connection(), xcb_leader, rootWindow(), NET::Properties(), NET::WM2StartupId);
    }
    effect_group = new render::effect_window_group_impl(this);
    space.groups.push_back(this);
}

group::~group()
{
    remove_all(space.groups, this);
    delete leader_info;
    delete effect_group;
}

QIcon group::icon() const
{
    if (leader) {
        return leader->control->icon();
    } else if (xcb_leader != XCB_WINDOW_NONE) {
        QIcon ic;
        NETWinInfo info(connection(), xcb_leader, rootWindow(), NET::WMIcon, NET::WM2IconPixmap);
        auto readIcon = [&ic, &info, this](int size, bool scale = true) {
            const QPixmap pix = KWindowSystem::icon(xcb_leader,
                                                    size,
                                                    size,
                                                    scale,
                                                    KWindowSystem::NETWM | KWindowSystem::WMHints,
                                                    &info);
            if (!pix.isNull()) {
                ic.addPixmap(pix);
            }
        };
        readIcon(16);
        readIcon(32);
        readIcon(48, false);
        readIcon(64, false);
        readIcon(128, false);
        return ic;
    }
    return QIcon();
}

void group::addMember(win::x11::window* member)
{
    members.push_back(member);
}

void group::removeMember(win::x11::window* member)
{
    assert(std::find(members.cbegin(), members.cend(), member) != members.cend());
    remove_all(members, member);
    // there are cases when automatic deleting of groups must be delayed,
    // e.g. when removing a member and doing some operation on the possibly
    // other members of the group (which would be however deleted already
    // if there were no other members)
    if (refcount == 0 && members.empty()) {
        delete this;
    }
}

void group::ref()
{
    ++refcount;
}

void group::deref()
{
    if (--refcount == 0 && members.empty()) {
        delete this;
    }
}

void group::gotLeader(win::x11::window* leader)
{
    assert(leader->xcb_window == xcb_leader);
    this->leader = leader;
}

void group::lostLeader()
{
    assert(std::find(members.cbegin(), members.cend(), leader) == members.cend());
    leader = nullptr;
    if (members.empty()) {
        delete this;
    }
}

//****************************************
// activation code
//****************************************

void group::startupIdChanged()
{
    KStartupInfoId asn_id;
    KStartupInfoData asn_data;
    auto asn_valid = check_startup_notification(space, xcb_leader, asn_id, asn_data);
    if (!asn_valid) {
        return;
    }

    if (asn_id.timestamp() != 0 && user_time != -1U
        && NET::timestampCompare(asn_id.timestamp(), user_time) > 0) {
        user_time = asn_id.timestamp();
    }
}

void group::updateUserTime(xcb_timestamp_t time)
{
    // copy of win::x11::update_user_time in control.h
    if (time == XCB_CURRENT_TIME) {
        kwinApp()->update_x11_time_from_clock();
        time = xTime();
    }
    if (time != -1U
        && (user_time == XCB_CURRENT_TIME
            || NET::timestampCompare(time, user_time) > 0)) // time > user_time
        user_time = time;
}

}
