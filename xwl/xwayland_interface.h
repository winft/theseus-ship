/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

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

#include "kwinglobals.h"

#include <QObject>
#include <QPoint>

namespace KWin::xwl
{

enum class drag_event_reply {
    // event should be ignored by the filter
    ignore,
    // event is filtered out
    take,
    // event should be handled as a Wayland native one
    wayland,
};

template<typename Space>
class xwayland_interface : public QObject
{
public:
    using window_t = typename Space::window_t;
    virtual drag_event_reply drag_move_filter(window_t* target, QPoint const& pos) = 0;
};

}
