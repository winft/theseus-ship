/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2018 Roman Gilg <subdiff@gmail.com>

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
#include <QSocketNotifier>
#include <QVector>

#include <xcb/xcb.h>

namespace KWin::Xwl
{

/**
 * Represents for an arbitrary selection a data transfer between
 * sender and receiver.
 *
 * Lives for the duration of the transfer and must be cleaned up
 * externally afterwards. For that the owner should connect to the
 * @c finished() signal.
 */
class Transfer : public QObject
{
    Q_OBJECT

public:
    Transfer(xcb_atom_t selection, qint32 fd, xcb_timestamp_t timestamp, QObject* parent = nullptr);

    virtual bool handle_property_notify(xcb_property_notify_event_t* event) = 0;
    void timeout();
    xcb_timestamp_t timestamp() const
    {
        return m_timestamp;
    }

Q_SIGNALS:
    void finished();

protected:
    void end_transfer();

    xcb_atom_t atom() const
    {
        return m_atom;
    }
    qint32 fd() const
    {
        return m_fd;
    }

    void set_incr(bool set)
    {
        m_incr = set;
    }
    bool incr() const
    {
        return m_incr;
    }
    void reset_timeout()
    {
        m_timeout = false;
    }

    void create_socket_notifier(QSocketNotifier::Type type);
    void clear_socket_notifier();
    QSocketNotifier* socket_notifier() const
    {
        return m_notifier;
    }

private:
    void close_fd();

    xcb_atom_t m_atom;
    qint32 m_fd;
    xcb_timestamp_t m_timestamp = XCB_CURRENT_TIME;

    QSocketNotifier* m_notifier = nullptr;
    bool m_incr = false;
    bool m_timeout = false;

    Q_DISABLE_COPY(Transfer)
};

/**
 * Represents a transfer from a Wayland native source to an X window.
 */
class TransferWltoX : public Transfer
{
    Q_OBJECT

public:
    TransferWltoX(xcb_atom_t selection,
                  xcb_selection_request_event_t* request,
                  qint32 fd,
                  QObject* parent = nullptr);
    ~TransferWltoX() override;

    void start_transfer_from_source();
    bool handle_property_notify(xcb_property_notify_event_t* event) override;

Q_SIGNALS:
    void selection_notify(xcb_selection_request_event_t* event, bool success);

private:
    void start_incr();
    void read_wl_source();
    int flush_source_data();
    void handle_property_delete();

    xcb_selection_request_event_t* m_request = nullptr;

    /* contains all received data portioned in chunks
     * TODO: explain second QPair component
     */
    QVector<QPair<QByteArray, int>> m_chunks;

    bool m_propertyIsSet = false;
    bool m_flushPropertyOnDelete = false;

    Q_DISABLE_COPY(TransferWltoX)
};

/**
 * Helper class for X to Wl transfers.
 */
class DataReceiver
{
public:
    virtual ~DataReceiver();

    void transfer_from_property(xcb_get_property_reply_t* reply);

    virtual void set_data(char const* value, int length);
    QByteArray data() const;

    void part_read(int length);

protected:
    void set_data_internal(QByteArray data)
    {
        m_data = data;
    }

private:
    xcb_get_property_reply_t* m_propertyReply = nullptr;
    int m_propertyStart = 0;
    QByteArray m_data;
};

/**
 * Compatibility receiver for clients only
 * supporting the NETSCAPE_URL scheme (Firefox)
 */
class NetscapeUrlReceiver : public DataReceiver
{
public:
    void set_data(char const* value, int length) override;
};

/**
 * Compatibility receiver for clients only
 * supporting the text/x-moz-url scheme (Chromium on own drags)
 */
class MozUrlReceiver : public DataReceiver
{
public:
    void set_data(char const* value, int length) override;
};

/**
 * Represents a transfer from an X window to a Wayland native client.
 */
class TransferXtoWl : public Transfer
{
    Q_OBJECT

public:
    TransferXtoWl(xcb_atom_t selection,
                  xcb_atom_t target,
                  qint32 fd,
                  xcb_timestamp_t timestamp,
                  xcb_window_t parentWindow,
                  x11_data const& x11,
                  QObject* parent = nullptr);
    ~TransferXtoWl() override;

    bool handle_selection_notify(xcb_selection_notify_event_t* event);
    bool handle_property_notify(xcb_property_notify_event_t* event) override;

private:
    void data_source_write();
    void start_transfer();
    void get_incr_chunk();

    xcb_window_t m_window;
    DataReceiver* m_receiver = nullptr;

    Q_DISABLE_COPY(TransferXtoWl)
};

}
