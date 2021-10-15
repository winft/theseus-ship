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
#include "drag.h"

#include "atoms.h"

namespace KWin::xwl
{

void drag::send_client_message(xcb_window_t target,
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

    auto con = kwinApp()->x11Connection();
    xcb_send_event(con, 0, target, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<char const*>(&event));
    xcb_flush(con);
}

dnd_action drag::atom_to_client_action(xcb_atom_t atom)
{
    if (atom == atoms->xdnd_action_copy) {
        return dnd_action::copy;
    } else if (atom == atoms->xdnd_action_move) {
        return dnd_action::move;
    } else if (atom == atoms->xdnd_action_ask) {
        // we currently do not support it - need some test client first
        return dnd_action::none;
        // return dnd_action::Ask;
    }
    return dnd_action::none;
}

xcb_atom_t drag::client_action_to_atom(dnd_action action)
{
    if (action == dnd_action::copy) {
        return atoms->xdnd_action_copy;
    } else if (action == dnd_action::move) {
        return atoms->xdnd_action_move;
    } else if (action == dnd_action::ask) {
        // we currently do not support it - need some test client first
        return XCB_ATOM_NONE;
        // return atoms->xdnd_action_ask;
    }
    return XCB_ATOM_NONE;
}

}
