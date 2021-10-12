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

#include <Wrapland/Server/drag_pool.h>

#include <QPoint>

#include <xcb/xcb.h>

namespace KWin
{
class Toplevel;

namespace Xwl
{
class Dnd;
enum class DragEventReply;

using DnDAction = Wrapland::Server::dnd_action;

/**
 * An ongoing drag operation.
 */
class Drag : public QObject
{
    Q_OBJECT

public:
    Drag() = default;

    static void
    send_client_message(xcb_window_t target, xcb_atom_t type, xcb_client_message_data_t* data);
    static DnDAction atom_to_client_action(xcb_atom_t atom);
    static xcb_atom_t client_action_to_atom(DnDAction action);

    virtual bool handle_client_message(xcb_client_message_event_t* event) = 0;
    virtual DragEventReply move_filter(Toplevel* target, QPoint const& pos) = 0;

    virtual bool end() = 0;

Q_SIGNALS:
    void finish(Drag* self);

private:
    Q_DISABLE_COPY(Drag)
};

}
}
