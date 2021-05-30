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

#ifndef KWIN_GROUP_H
#define KWIN_GROUP_H

#include "utils.h"
#include <netwm.h>

#include <vector>

namespace KWin
{

class EffectWindowGroupImpl;

namespace win::x11
{
class window;
}

class Group
{
public:
    Group(xcb_window_t leader);
    ~Group();
    xcb_window_t leader() const;
    const win::x11::window* leaderClient() const;
    win::x11::window* leaderClient();
    std::vector<win::x11::window*> const& members() const;
    QIcon icon() const;
    void addMember(win::x11::window* member);
    void removeMember(win::x11::window* member);
    void gotLeader(win::x11::window* leader);
    void lostLeader();
    void updateUserTime(xcb_timestamp_t time);
    xcb_timestamp_t userTime() const;
    void ref();
    void deref();
    EffectWindowGroupImpl* effectGroup();

private:
    void startupIdChanged();
    std::vector<win::x11::window*> _members;
    win::x11::window* leader_client;
    xcb_window_t leader_wid;
    NETWinInfo* leader_info;
    xcb_timestamp_t user_time;
    int refcount;
    EffectWindowGroupImpl* effect_group;
};

inline xcb_window_t Group::leader() const
{
    return leader_wid;
}

inline const win::x11::window* Group::leaderClient() const
{
    return leader_client;
}

inline win::x11::window* Group::leaderClient()
{
    return leader_client;
}

inline std::vector<win::x11::window*> const& Group::members() const
{
    return _members;
}

inline xcb_timestamp_t Group::userTime() const
{
    return user_time;
}

inline EffectWindowGroupImpl* Group::effectGroup()
{
    return effect_group;
}

} // namespace

#endif
