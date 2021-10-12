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
#include <QVector>
#include <memory>
#include <vector>

namespace KWin
{
class Toplevel;

namespace Xwl
{
enum class DragEventReply;
class WlVisit;
template<typename>
class X11Source;

using Mimes = QVector<QPair<QString, xcb_atom_t>>;
using DataX11Source = X11Source<data_source_ext>;

class XToWlDrag : public Drag
{
    Q_OBJECT

public:
    explicit XToWlDrag(DataX11Source* source, Dnd* dnd);
    ~XToWlDrag() override;

    DragEventReply move_filter(Toplevel* target, QPoint const& pos) override;
    bool handle_client_message(xcb_client_message_event_t* event) override;
    bool end() override;

private:
    void set_offers(Mimes const& offers);
    void set_drag_target();

    bool check_for_finished();

    Mimes m_offers;
    Mimes m_offersPending;

    DataX11Source* m_source;
    QVector<QPair<xcb_timestamp_t, bool>> m_dataRequests;

    std::unique_ptr<WlVisit> m_visit;
    std::vector<std::unique_ptr<WlVisit>> m_oldVisits;

    bool m_performed = false;

    std::unique_ptr<data_source_ext> data_source;
    Dnd* dnd;

    Q_DISABLE_COPY(XToWlDrag)
};

class WlVisit : public QObject
{
    Q_OBJECT

public:
    WlVisit(Toplevel* target, DataX11Source* source);
    ~WlVisit() override;

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
    void offers_received(Mimes const& offers);
    void finish(WlVisit* self);

private:
    bool handle_enter(xcb_client_message_event_t* event);
    bool handle_position(xcb_client_message_event_t* event);
    bool handle_drop(xcb_client_message_event_t* event);
    bool handle_leave(xcb_client_message_event_t* event);

    void send_status();

    void get_mimes_from_win_property(Mimes& offers);

    bool target_accepts_action() const;

    void do_finish();
    void unmap_proxy_window();

    Toplevel* m_target;
    xcb_window_t m_window;

    xcb_window_t m_srcWindow = XCB_WINDOW_NONE;
    DataX11Source* source;

    uint32_t m_version = 0;

    xcb_atom_t m_actionAtom;
    DnDAction m_action = DnDAction::none;

    bool m_mapped = false;
    bool m_entered = false;
    bool m_dropHandled = false;
    bool m_finished = false;

    Q_DISABLE_COPY(WlVisit)
};

}
}
