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

#include "xwayland.h"

#include <QObject>
#include <QVector>

#include <xcb/xcb.h>

class QSocketNotifier;

struct xcb_selection_request_event_t;
struct xcb_xfixes_selection_notify_event_t;

namespace KWin::Xwl
{

/*
 * QObject attribute of a WlSource.
 * This is a hack around having a template QObject.
 */
class qWlSource : public QObject
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
class WlSource
{
public:
    WlSource(ServerSource* source, xcb_connection_t* connection);
    ~WlSource();

    bool handle_selection_request(xcb_selection_request_event_t* event);

    xcb_timestamp_t timestamp() const
    {
        return m_timestamp;
    }
    void set_timestamp(xcb_timestamp_t time)
    {
        m_timestamp = time;
    }

    qWlSource* qobject() const
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

    QVector<QString> m_offers;
    QMetaObject::Connection m_offerConnection;

    xcb_timestamp_t m_timestamp = XCB_CURRENT_TIME;
    qWlSource* m_qobject;

    Q_DISABLE_COPY(WlSource)
};

using Mimes = QVector<QPair<QString, xcb_atom_t>>;

/*
 * QObject attribute of a X11Source.
 * This is a hack around having a template QObject.
 */
class qX11Source : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    void offers_changed(QStringList const& added, QStringList const& removed);
    void transfer_ready(xcb_atom_t target, qint32 fd);
};

/**
 * Representing an X data source.
 */
template<typename InternalSource>
class X11Source
{
public:
    X11Source(xcb_xfixes_selection_notify_event_t* event, x11_data const& x11);
    ~X11Source();

    /**
     * @param ds must exist.
     *
     * X11Source does not take ownership of it in general, but if the function
     * is called again, it will delete the previous data source.
     */
    void set_source(InternalSource* src);
    InternalSource* source() const
    {
        return m_source;
    }
    void get_targets(xcb_window_t const window, xcb_atom_t const atom) const;

    Mimes offers() const
    {
        return m_offers;
    }
    void set_offers(Mimes const& offers);

    bool handle_selection_notify(xcb_selection_notify_event_t* event);

    xcb_timestamp_t timestamp() const
    {
        return m_timestamp;
    }
    void set_timestamp(xcb_timestamp_t time)
    {
        m_timestamp = time;
    }

    qX11Source* qobject() const
    {
        return m_qobject;
    }

    x11_data const x11;

private:
    void handle_targets(xcb_window_t const requestor);
    void start_transfer(QString const& mimeName, qint32 fd);

    xcb_window_t m_owner;
    InternalSource* m_source = nullptr;

    Mimes m_offers;

    xcb_timestamp_t m_timestamp = XCB_CURRENT_TIME;
    qX11Source* m_qobject;

    Q_DISABLE_COPY(X11Source)
};

}
