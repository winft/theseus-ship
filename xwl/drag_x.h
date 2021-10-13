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
#include "types.h"

#include <QPoint>
#include <memory>
#include <utility>
#include <vector>

namespace KWin
{
class Toplevel;

namespace xwl
{
enum class drag_event_reply;
class wl_visit;
template<typename>
class x11_source;

using x11_source_ext = x11_source<data_source_ext>;

class x11_drag : public drag
{
    Q_OBJECT

public:
    explicit x11_drag(x11_source_ext* source);
    ~x11_drag() override;

    drag_event_reply move_filter(Toplevel* target, QPoint const& pos) override;
    bool handle_client_message(xcb_client_message_event_t* event) override;
    bool end() override;

    void handle_transfer_finished(xcb_timestamp_t time);

    std::unique_ptr<data_source_ext> data_source;
    std::unique_ptr<wl_visit> m_visit;

private:
    void set_offers(mime_atoms const& offers);
    void set_drag_target();

    bool check_for_finished();

    mime_atoms m_offers;
    mime_atoms m_offersPending;

    x11_source_ext* m_source;
    std::vector<std::pair<xcb_timestamp_t, bool>> m_dataRequests;

    std::vector<std::unique_ptr<wl_visit>> m_oldVisits;

    bool m_performed = false;

    Q_DISABLE_COPY(x11_drag)
};

class wl_visit : public QObject
{
    Q_OBJECT

public:
    wl_visit(Toplevel* target, x11_source_ext* source);
    ~wl_visit() override;

    bool handle_client_message(xcb_client_message_event_t* event);
    bool leave();

    Toplevel* target() const
    {
        return m_target;
    }
    xcb_window_t window() const
    {
        return m_window;
    }
    bool entered() const
    {
        return m_entered;
    }
    bool drop_handled() const
    {
        return m_dropHandled;
    }
    bool finished() const
    {
        return m_finished;
    }
    void send_finished();

Q_SIGNALS:
    void offers_received(mime_atoms const& offers);
    void finish(wl_visit* self);

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

    Toplevel* m_target;
    xcb_window_t m_window;

    xcb_window_t m_srcWindow = XCB_WINDOW_NONE;
    x11_source_ext* source;

    uint32_t m_version = 0;

    xcb_atom_t m_actionAtom;
    dnd_action m_action = dnd_action::none;

    bool m_mapped = false;
    bool m_entered = false;
    bool m_dropHandled = false;
    bool m_finished = false;

    Q_DISABLE_COPY(wl_visit)
};

}
}
