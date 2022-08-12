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

#include "drag.h"
#include "sources.h"

#include <QPoint>
#include <memory>

namespace Wrapland::Server
{
class data_source;
}

namespace KWin
{
class Toplevel;

namespace xwl
{
enum class drag_event_reply;
class x11_visit;

using dnd_actions = Wrapland::Server::dnd_actions;

class wl_drag : public drag
{
    Q_OBJECT

public:
    wl_drag(wl_source<Wrapland::Server::data_source> const& source, xcb_window_t proxy_window);

    drag_event_reply move_filter(Toplevel* target, QPoint const& pos) override;
    bool handle_client_message(xcb_client_message_event_t* event) override;
    bool end() override;

private:
    wl_source<Wrapland::Server::data_source> const& source;
    xcb_window_t proxy_window;
    std::unique_ptr<x11_visit> visit;

    Q_DISABLE_COPY(wl_drag)
};

}
}
