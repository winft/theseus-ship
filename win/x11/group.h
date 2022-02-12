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
#pragma once

#include "kwin_export.h"

#include <QIcon>
#include <netwm.h>
#include <vector>

namespace KWin
{

namespace render
{
class effect_window_group_impl;
}

namespace win::x11
{
class window;

class KWIN_EXPORT group
{
public:
    group(xcb_window_t leader);
    ~group();
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
    render::effect_window_group_impl* effectGroup();

private:
    void startupIdChanged();
    std::vector<win::x11::window*> _members;
    win::x11::window* leader_client;
    xcb_window_t leader_wid;
    NETWinInfo* leader_info;
    xcb_timestamp_t user_time;
    int refcount;
    render::effect_window_group_impl* effect_group;
};

inline xcb_window_t group::leader() const
{
    return leader_wid;
}

inline const win::x11::window* group::leaderClient() const
{
    return leader_client;
}

inline win::x11::window* group::leaderClient()
{
    return leader_client;
}

inline std::vector<win::x11::window*> const& group::members() const
{
    return _members;
}

inline xcb_timestamp_t group::userTime() const
{
    return user_time;
}

inline render::effect_window_group_impl* group::effectGroup()
{
    return effect_group;
}

}
}
