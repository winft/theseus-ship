/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "drag.h"
#include "types.h"

#include <QObject>
#include <memory>

namespace KWin
{

class Toplevel;

namespace xwl
{

class data_source_ext;

template<typename>
class x11_source;
using x11_source_ext = x11_source<data_source_ext>;

class wl_visit_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void offers_received(mime_atoms const& offers);
    void finish();
};

class wl_visit
{
public:
    wl_visit(Toplevel* target, x11_source_ext& source);
    ~wl_visit();

    bool handle_client_message(xcb_client_message_event_t* event);
    bool leave();

    void send_finished();

    std::unique_ptr<wl_visit_qobject> qobject;

    Toplevel* target;
    xcb_window_t window;

    struct {
        bool mapped{false};
        bool entered{false};
        bool drop_handled{false};
        bool finished{false};
    } state;

private:
    bool handle_enter(xcb_client_message_event_t* event);
    bool handle_position(xcb_client_message_event_t* event);
    bool handle_drop(xcb_client_message_event_t* event);
    bool handle_leave(xcb_client_message_event_t* event);

    void send_status();

    void get_mimes_from_win_property(mime_atoms& offers);

    bool target_accepts_action() const;

    void do_finish();
    void unmap_proxy_window();

    xcb_window_t source_window = XCB_WINDOW_NONE;
    x11_source_ext& source;

    uint32_t m_version = 0;

    xcb_atom_t action_atom{XCB_NONE};
    dnd_action action = dnd_action::none;

    Q_DISABLE_COPY(wl_visit)
};

}
}
