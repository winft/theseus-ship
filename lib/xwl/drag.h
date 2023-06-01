/*
SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <QObject>
#include <QPoint>
#include <Wrapland/Server/drag_pool.h>
#include <kwin_export.h>
#include <memory>
#include <xcb/xcb.h>

namespace KWin::xwl
{

// version of DnD support in X
constexpr uint32_t drag_and_drop_version = 5;

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

class KWIN_EXPORT drag_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void finish();
};

/**
 * An ongoing drag operation.
 */
template<typename Space>
class drag
{
public:
    drag()
        : qobject{std::make_unique<drag_qobject>()}
    {
    }

    virtual ~drag() = default;

    virtual bool handle_client_message(xcb_client_message_event_t* event) = 0;
    virtual drag_event_reply move_filter(std::optional<typename Space::window_t> target,
                                         QPoint const& pos)
        = 0;

    virtual bool end() = 0;

    std::unique_ptr<drag_qobject> qobject;
};

}
