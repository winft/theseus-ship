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

#include "types.h"

#include <QPoint>
#include <Wrapland/Server/drag_pool.h>
#include <xcb/xcb.h>

namespace KWin
{
class Toplevel;

namespace xwl
{
class drag_and_drop;
enum class drag_event_reply;

using dnd_action = Wrapland::Server::dnd_action;

inline dnd_action atom_to_client_action(xcb_atom_t atom, base::x11::atoms const& atoms)
{
    if (atom == atoms.xdnd_action_copy) {
        return dnd_action::copy;
    } else if (atom == atoms.xdnd_action_move) {
        return dnd_action::move;
    } else if (atom == atoms.xdnd_action_ask) {
        // we currently do not support it - need some test client first
        return dnd_action::none;
        // return dnd_action::Ask;
    }
    return dnd_action::none;
}

inline xcb_atom_t client_action_to_atom(dnd_action action, base::x11::atoms const& atoms)
{
    if (action == dnd_action::copy) {
        return atoms.xdnd_action_copy;
    } else if (action == dnd_action::move) {
        return atoms.xdnd_action_move;
    } else if (action == dnd_action::ask) {
        // we currently do not support it - need some test client first
        return XCB_ATOM_NONE;
        // return atoms.xdnd_action_ask;
    }
    return XCB_ATOM_NONE;
}

inline void send_client_message(xcb_connection_t* connection,
                                xcb_window_t target,
                                xcb_atom_t type,
                                xcb_client_message_data_t* data)
{
    xcb_client_message_event_t event{
        XCB_CLIENT_MESSAGE, // response_type
        32,                 // format
        0,                  // sequence
        target,             // window
        type,               // type
        *data,              // data
    };

    xcb_send_event(
        connection, 0, target, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<char const*>(&event));
    xcb_flush(connection);
}

/**
 * An ongoing drag operation.
 */
class drag : public QObject
{
    Q_OBJECT

public:
    drag();

    virtual bool handle_client_message(xcb_client_message_event_t* event) = 0;
    virtual drag_event_reply move_filter(Toplevel* target, QPoint const& pos) = 0;

    virtual bool end() = 0;

Q_SIGNALS:
    void finish(drag* self);

private:
    Q_DISABLE_COPY(drag)
};

}
}
