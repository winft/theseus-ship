/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "drag.h"
#include "sources.h"

#include <QObject>
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

class x11_visit_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void finish();
};

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
