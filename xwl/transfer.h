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
#include <deque>

#include <xcb/xcb.h>

namespace KWin::xwl
{

/**
 * Represents for an arbitrary selection a data transfer between
 * sender and receiver.
 *
 * Lives for the duration of the transfer and must be cleaned up
 * externally afterwards. For that the owner should connect to the
 * @c finished() signal.
 */
class KWIN_EXPORT transfer : public QObject
{
    Q_OBJECT

public:
    transfer(xcb_atom_t selection,
             qint32 fd,
             xcb_timestamp_t timestamp,
             base::x11::atoms const& atoms,
             QObject* parent = nullptr);

    virtual bool handle_property_notify(xcb_property_notify_event_t* event) = 0;
    void timeout();
    xcb_timestamp_t get_timestamp() const
    {
        return timestamp;
    }

    base::x11::atoms const& atoms;

Q_SIGNALS:
    void finished();

protected:
    void end_transfer();

    xcb_atom_t get_atom() const
    {
        return atom;
    }
    qint32 get_fd() const
    {
        return fd;
    }

    void set_incr(bool set)
    {
        incr = set;
    }
    bool get_incr() const
    {
        return incr;
    }
    void reset_timeout()
    {
        timed_out = false;
    }

    void create_socket_notifier(QSocketNotifier::Type type);
    void clear_socket_notifier();
    QSocketNotifier* socket_notifier() const
    {
        return notifier;
    }

private:
    void close_fd();

    xcb_atom_t atom;
    qint32 fd;
    xcb_timestamp_t timestamp{XCB_CURRENT_TIME};

    QSocketNotifier* notifier = nullptr;
    bool incr = false;
    bool timed_out = false;

    Q_DISABLE_COPY(transfer)
};

/**
 * Represents a transfer from a Wayland native source to an X window.
 */
class KWIN_EXPORT wl_to_x11_transfer : public transfer
{
    Q_OBJECT

public:
    wl_to_x11_transfer(xcb_atom_t selection,
                       xcb_selection_request_event_t* request,
                       qint32 fd,
                       base::x11::atoms const& atoms,
                       QObject* parent = nullptr);
    ~wl_to_x11_transfer() override;

    void start_transfer_from_source();
    bool handle_property_notify(xcb_property_notify_event_t* event) override;

Q_SIGNALS:
    void selection_notify(xcb_selection_request_event_t* event, bool success);

private:
    void start_incr();
    void read_wl_source();
    int flush_source_data();
    void handle_property_delete();

    xcb_selection_request_event_t* request = nullptr;

    /* contains all received data portioned in chunks
     * TODO(romangg): explain second std::pair component
     */
    std::deque<std::pair<QByteArray, int>> chunks;

    bool property_is_set = false;
    bool flush_property_on_delete = false;

    Q_DISABLE_COPY(wl_to_x11_transfer)
};

/**
 * Helper class for X to Wl transfers.
 */
class data_receiver
{
public:
    virtual ~data_receiver();

    void transfer_from_property(xcb_get_property_reply_t* reply);

    virtual void set_data(char const* value, int length);
    QByteArray get_data() const;

    void part_read(int length);

protected:
    void set_data_internal(QByteArray data)
    {
        this->data = data;
    }

private:
    xcb_get_property_reply_t* property_reply = nullptr;
    int property_start = 0;
    QByteArray data;
};

/**
 * Compatibility receiver for clients only
 * supporting the NETSCAPE_URL scheme (Firefox)
 */
class netscape_url_receiver : public data_receiver
{
public:
    void set_data(char const* value, int length) override;
};

/**
 * Compatibility receiver for clients only
 * supporting the text/x-moz-url scheme (Chromium on own drags)
 */
class moz_url_receiver : public data_receiver
{
public:
    void set_data(char const* value, int length) override;
};

/**
 * Represents a transfer from an X window to a Wayland native client.
 */
class KWIN_EXPORT x11_to_wl_transfer : public transfer
{
    Q_OBJECT

public:
    x11_to_wl_transfer(xcb_atom_t selection,
                       xcb_atom_t target,
                       qint32 fd,
                       xcb_timestamp_t timestamp,
                       xcb_window_t parentWindow,
                       x11_runtime const& x11,
                       QObject* parent = nullptr);
    ~x11_to_wl_transfer() override;

    bool handle_selection_notify(xcb_selection_notify_event_t* event);
    bool handle_property_notify(xcb_property_notify_event_t* event) override;

private:
    void data_source_write();
    void start_transfer();
    void get_incr_chunk();

    xcb_window_t window;
    data_receiver* receiver = nullptr;

    Q_DISABLE_COPY(x11_to_wl_transfer)
};

}
