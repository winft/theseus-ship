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

#include <QObject>
#include <vector>
#include <xcb/xcb.h>

class QSocketNotifier;

struct xcb_selection_request_event_t;
struct xcb_xfixes_selection_notify_event_t;

namespace KWin::xwl
{

/*
 * QObject attribute of a wl_source.
 * This is a hack around having a template QObject.
 */
class q_wl_source : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    void transfer_ready(xcb_selection_request_event_t* event, qint32 fd);
};

/**
 * Representing a Wayland native data source.
 */
template<typename ServerSource>
class wl_source
{
public:
    wl_source(ServerSource* source, xcb_connection_t* connection);
    ~wl_source();

    bool handle_selection_request(xcb_selection_request_event_t* event);

    xcb_timestamp_t timestamp() const
    {
        return m_timestamp;
    }
    void set_timestamp(xcb_timestamp_t time)
    {
        m_timestamp = time;
    }

    q_wl_source* qobject() const
    {
        return m_qobject;
    }

private:
    void send_targets(xcb_selection_request_event_t* event);
    void send_timestamp(xcb_selection_request_event_t* event);

    void receive_offer(std::string const& mime);
    bool check_start_transfer(xcb_selection_request_event_t* event);

    ServerSource* server_source = nullptr;
    xcb_connection_t* connection;

    std::vector<std::string> m_offers;

    xcb_timestamp_t m_timestamp = XCB_CURRENT_TIME;
    q_wl_source* m_qobject;

    Q_DISABLE_COPY(wl_source)
};

/*
 * QObject attribute of a x11_source.
 * This is a hack around having a template QObject.
 */
class q_x11_source : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    void offers_changed(std::vector<std::string> const& added,
                        std::vector<std::string> const& removed);
    void transfer_ready(xcb_atom_t target, qint32 fd);
};

/**
 * Representing an X data source.
 */
template<typename InternalSource>
class x11_source
{
public:
    x11_source(xcb_xfixes_selection_notify_event_t* event, x11_data const& x11);
    ~x11_source();

    /**
     * @param ds must exist.
     *
     * x11_source does not take ownership of it in general, but if the function
     * is called again, it will delete the previous data source.
     */
    void set_source(InternalSource* src);
    InternalSource* source() const
    {
        return m_source;
    }
    void get_targets(xcb_window_t const window, xcb_atom_t const atom) const;

    mime_atoms offers() const
    {
        return m_offers;
    }
    void set_offers(mime_atoms const& offers);

    bool handle_selection_notify(xcb_selection_notify_event_t* event);

    xcb_timestamp_t timestamp() const
    {
        return m_timestamp;
    }
    void set_timestamp(xcb_timestamp_t time)
    {
        m_timestamp = time;
    }

    q_x11_source* qobject() const
    {
        return m_qobject;
    }

    x11_data const x11;

private:
    void handle_targets(xcb_window_t const requestor);
    void start_transfer(std::string const& mimeName, qint32 fd);

    xcb_window_t m_owner;
    InternalSource* m_source = nullptr;

    mime_atoms m_offers;

    xcb_timestamp_t m_timestamp = XCB_CURRENT_TIME;
    q_x11_source* m_qobject;

    Q_DISABLE_COPY(x11_source)
};

}

Q_DECLARE_METATYPE(std::vector<std::string>)
