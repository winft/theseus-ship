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

namespace win
{

class space;

namespace x11
{

class window;

class KWIN_EXPORT group
{
public:
    group(xcb_window_t xcb_leader, win::space& space);
    ~group();

    QIcon icon() const;
    void addMember(win::x11::window* member);
    void removeMember(win::x11::window* member);
    void gotLeader(win::x11::window* leader);
    void lostLeader();
    void updateUserTime(xcb_timestamp_t time);
    void ref();
    void deref();

    std::vector<win::x11::window*> members;
    win::x11::window* leader{nullptr};
    xcb_window_t xcb_leader;
    NETWinInfo* leader_info{nullptr};
    xcb_timestamp_t user_time{-1U};
    render::effect_window_group_impl* effect_group;

private:
    void startupIdChanged();
    int refcount{0};
    win::space& space;
};

template<typename Space>
group* find_group(Space& space, xcb_window_t leader)
{
    assert(leader != XCB_WINDOW_NONE);
    for (auto group : space.groups) {
        if (group->xcb_leader == leader) {
            return group;
        }
    }
    return nullptr;
}

}
}
}
