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

class x11_visit_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void finish();
};

/// Visit to an X window
class x11_visit
{
public:
    // TODO: handle ask action

    x11_visit(Toplevel* target,
              wl_source<Wrapland::Server::data_source> const& source,
              xcb_window_t drag_window);

    bool handle_client_message(xcb_client_message_event_t* event);

    void send_position(QPointF const& globalPos);
    void leave();

    std::unique_ptr<x11_visit_qobject> qobject;

    Toplevel* target;

    struct {
        bool entered{false};
        bool dropped{false};
        bool finished{false};
    } state;

private:
    bool handle_status(xcb_client_message_event_t* event);
    bool handle_finished(xcb_client_message_event_t* event);

    void send_enter();
    void send_drop(uint32_t time);
    void send_leave();

    void receive_offer();
    void enter();
    void update_actions();
    void drop();

    void do_finish();
    void stop_connections();

    wl_source<Wrapland::Server::data_source> const& source;
    xcb_window_t drag_window;
    uint32_t version = 0;

    struct {
        QMetaObject::Connection motion;
        QMetaObject::Connection action;
        QMetaObject::Connection drop;
    } notifiers;

    struct {
        bool pending = false;
        bool cached = false;
        QPoint cache;
    } m_pos;

    struct {
        // Preferred by the X client.
        dnd_action preferred{dnd_action::none};
        // Decided upon by the compositor.
        dnd_action proposed{dnd_action::none};
    } actions;

    bool m_accepts = false;

    Q_DISABLE_COPY(x11_visit)
};

}
}
