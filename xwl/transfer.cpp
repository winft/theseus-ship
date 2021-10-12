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
#include "transfer.h"

#include "atoms.h"

#include <algorithm>
#include <unistd.h>

#include <xwayland_logging.h>

namespace KWin::Xwl
{

// in Bytes: equals 64KB
constexpr uint32_t s_incrChunkSize = 63 * 1024;

Transfer::Transfer(xcb_atom_t selection, qint32 fd, xcb_timestamp_t timestamp, QObject* parent)
    : QObject(parent)
    , m_atom(selection)
    , m_fd(fd)
    , m_timestamp(timestamp)
{
}

void Transfer::create_socket_notifier(QSocketNotifier::Type type)
{
    delete m_notifier;
    m_notifier = new QSocketNotifier(m_fd, type, this);
}

void Transfer::clear_socket_notifier()
{
    delete m_notifier;
    m_notifier = nullptr;
}

void Transfer::timeout()
{
    if (m_timeout) {
        end_transfer();
    }
    m_timeout = true;
}

void Transfer::end_transfer()
{
    clear_socket_notifier();
    close_fd();
    Q_EMIT finished();
}

void Transfer::close_fd()
{
    if (m_fd < 0) {
        return;
    }
    close(m_fd);
    m_fd = -1;
}

TransferWltoX::TransferWltoX(xcb_atom_t selection,
                             xcb_selection_request_event_t* request,
                             qint32 fd,
                             QObject* parent)
    : Transfer(selection, fd, 0, parent)
    , m_request(request)
{
}

TransferWltoX::~TransferWltoX()
{
    delete m_request;
    m_request = nullptr;
}

void TransferWltoX::start_transfer_from_source()
{
    create_socket_notifier(QSocketNotifier::Read);
    connect(socket_notifier(), &QSocketNotifier::activated, this, [this](int socket) {
        Q_UNUSED(socket);
        read_wl_source();
    });
}

int TransferWltoX::flush_source_data()
{
    auto xcbConn = kwinApp()->x11Connection();

    xcb_change_property(xcbConn,
                        XCB_PROP_MODE_REPLACE,
                        m_request->requestor,
                        m_request->property,
                        m_request->target,
                        8,
                        m_chunks.first().first.size(),
                        m_chunks.first().first.data());
    xcb_flush(xcbConn);

    m_propertyIsSet = true;
    reset_timeout();

    auto const rm = m_chunks.takeFirst();
    return rm.first.size();
}

void TransferWltoX::start_incr()
{
    Q_ASSERT(m_chunks.size() == 1);

    auto xcbConn = kwinApp()->x11Connection();

    uint32_t mask[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(xcbConn, m_request->requestor, XCB_CW_EVENT_MASK, mask);

    // spec says to make the available space larger
    uint32_t const chunkSpace = 1024 + s_incrChunkSize;
    xcb_change_property(xcbConn,
                        XCB_PROP_MODE_REPLACE,
                        m_request->requestor,
                        m_request->property,
                        atoms->incr,
                        32,
                        1,
                        &chunkSpace);
    xcb_flush(xcbConn);

    set_incr(true);
    // first data will be flushed after the property has been deleted
    // again by the requestor
    m_flushPropertyOnDelete = true;
    m_propertyIsSet = true;
    Q_EMIT selection_notify(m_request, true);
}

void TransferWltoX::read_wl_source()
{
    if (m_chunks.size() == 0 || m_chunks.last().second == s_incrChunkSize) {
        // append new chunk
        auto next = QPair<QByteArray, int>();
        next.first.resize(s_incrChunkSize);
        next.second = 0;
        m_chunks.append(next);
    }

    auto const oldLen = m_chunks.last().second;
    auto const avail = s_incrChunkSize - m_chunks.last().second;
    Q_ASSERT(avail > 0);

    ssize_t readLen = read(fd(), m_chunks.last().first.data() + oldLen, avail);
    if (readLen == -1) {
        qCWarning(KWIN_XWL) << "Error reading in Wl data.";

        // TODO: cleanup X side?
        end_transfer();
        return;
    }
    m_chunks.last().second = oldLen + readLen;

    if (readLen == 0) {
        // at the fd end - complete transfer now
        m_chunks.last().first.resize(m_chunks.last().second);

        if (incr()) {
            // incremental transfer is to be completed now
            m_flushPropertyOnDelete = true;
            if (!m_propertyIsSet) {
                // flush if target's property is not set at the moment
                flush_source_data();
            }
            clear_socket_notifier();
        } else {
            // non incremental transfer is to be completed now,
            // data can be transferred to X client via a single property set
            flush_source_data();
            Q_EMIT selection_notify(m_request, true);
            end_transfer();
        }
    } else if (m_chunks.last().second == s_incrChunkSize) {
        // first chunk full, but not yet at fd end -> go incremental
        if (incr()) {
            m_flushPropertyOnDelete = true;
            if (!m_propertyIsSet) {
                // flush if target's property is not set at the moment
                flush_source_data();
            }
        } else {
            // starting incremental transfer
            start_incr();
        }
    }
    reset_timeout();
}

bool TransferWltoX::handle_property_notify(xcb_property_notify_event_t* event)
{
    if (event->window == m_request->requestor) {
        if (event->state == XCB_PROPERTY_DELETE && event->atom == m_request->property) {
            handle_property_delete();
        }
        return true;
    }
    return false;
}

void TransferWltoX::handle_property_delete()
{
    if (!incr()) {
        // non-incremental transfer: nothing to do
        return;
    }
    m_propertyIsSet = false;

    if (m_flushPropertyOnDelete) {
        if (!socket_notifier() && m_chunks.isEmpty()) {
            // transfer complete
            auto xcbConn = kwinApp()->x11Connection();

            uint32_t mask[] = {0};
            xcb_change_window_attributes(xcbConn, m_request->requestor, XCB_CW_EVENT_MASK, mask);

            xcb_change_property(xcbConn,
                                XCB_PROP_MODE_REPLACE,
                                m_request->requestor,
                                m_request->property,
                                m_request->target,
                                8,
                                0,
                                nullptr);
            xcb_flush(xcbConn);
            m_flushPropertyOnDelete = false;
            end_transfer();
        } else {
            flush_source_data();
        }
    }
}

TransferXtoWl::TransferXtoWl(xcb_atom_t selection,
                             xcb_atom_t target,
                             qint32 fd,
                             xcb_timestamp_t timestamp,
                             xcb_window_t parentWindow,
                             x11_data const& x11,
                             QObject* parent)
    : Transfer(selection, fd, timestamp, parent)
{
    // create transfer window
    auto xcbConn = kwinApp()->x11Connection();
    m_window = xcb_generate_id(xcbConn);
    uint32_t const values[] = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_create_window(xcbConn,
                      XCB_COPY_FROM_PARENT,
                      m_window,
                      parentWindow,
                      0,
                      0,
                      10,
                      10,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      x11.screen->root_visual,
                      XCB_CW_EVENT_MASK,
                      values);
    // convert selection
    xcb_convert_selection(xcbConn, m_window, selection, target, atoms->wl_selection, timestamp);
    xcb_flush(xcbConn);
}

TransferXtoWl::~TransferXtoWl()
{
    auto xcbConn = kwinApp()->x11Connection();
    xcb_destroy_window(xcbConn, m_window);
    xcb_flush(xcbConn);

    delete m_receiver;
    m_receiver = nullptr;
}

bool TransferXtoWl::handle_property_notify(xcb_property_notify_event_t* event)
{
    if (event->window == m_window) {
        if (event->state == XCB_PROPERTY_NEW_VALUE && event->atom == atoms->wl_selection) {
            get_incr_chunk();
        }
        return true;
    }
    return false;
}

bool TransferXtoWl::handle_selection_notify(xcb_selection_notify_event_t* event)
{
    if (event->requestor != m_window) {
        return false;
    }
    if (event->selection != atom()) {
        return false;
    }
    if (event->property == XCB_ATOM_NONE) {
        qCWarning(KWIN_XWL) << "Incoming X selection conversion failed";
        return true;
    }
    if (event->target == atoms->targets) {
        qCWarning(KWIN_XWL) << "Received targets too late";
        // TODO: or allow it?
        return true;
    }
    if (m_receiver) {
        // second selection notify element - misbehaving source

        // TODO: cancel this transfer?
        return true;
    }

    if (event->target == atoms->netscape_url) {
        m_receiver = new NetscapeUrlReceiver;
    } else if (event->target == atoms->moz_url) {
        m_receiver = new MozUrlReceiver;
    } else {
        m_receiver = new DataReceiver;
    }
    start_transfer();
    return true;
}

void TransferXtoWl::start_transfer()
{
    auto xcbConn = kwinApp()->x11Connection();
    auto cookie = xcb_get_property(
        xcbConn, 1, m_window, atoms->wl_selection, XCB_GET_PROPERTY_TYPE_ANY, 0, 0x1fffffff);

    auto reply = xcb_get_property_reply(xcbConn, cookie, nullptr);
    if (reply == nullptr) {
        qCWarning(KWIN_XWL) << "Can't get selection property.";
        end_transfer();
        return;
    }

    if (reply->type == atoms->incr) {
        set_incr(true);
        free(reply);
    } else {
        set_incr(false);
        // reply's ownership is transferred
        m_receiver->transfer_from_property(reply);
        data_source_write();
    }
}

void TransferXtoWl::get_incr_chunk()
{
    if (!incr()) {
        // source tries to sent incrementally, but did not announce it before
        return;
    }
    if (!m_receiver) {
        // receive mechanism has not yet been setup
        return;
    }
    auto xcbConn = kwinApp()->x11Connection();

    auto cookie = xcb_get_property(
        xcbConn, 0, m_window, atoms->wl_selection, XCB_GET_PROPERTY_TYPE_ANY, 0, 0x1fffffff);

    auto reply = xcb_get_property_reply(xcbConn, cookie, nullptr);
    if (!reply) {
        qCWarning(KWIN_XWL) << "Can't get selection property.";
        end_transfer();
        return;
    }

    if (xcb_get_property_value_length(reply) > 0) {
        // reply's ownership is transferred
        m_receiver->transfer_from_property(reply);
        data_source_write();
    } else {
        // Transfer complete
        free(reply);
        end_transfer();
    }
}

DataReceiver::~DataReceiver()
{
    if (m_propertyReply) {
        free(m_propertyReply);
        m_propertyReply = nullptr;
    }
}

void DataReceiver::transfer_from_property(xcb_get_property_reply_t* reply)
{
    m_propertyStart = 0;
    m_propertyReply = reply;

    set_data(static_cast<char*>(xcb_get_property_value(reply)),
             xcb_get_property_value_length(reply));
}

void DataReceiver::set_data(char const* value, int length)
{
    // simply set data without copy
    m_data = QByteArray::fromRawData(value, length);
}

QByteArray DataReceiver::data() const
{
    return QByteArray::fromRawData(m_data.data() + m_propertyStart,
                                   m_data.size() - m_propertyStart);
}

void DataReceiver::part_read(int length)
{
    m_propertyStart += length;
    if (m_propertyStart == m_data.size()) {
        Q_ASSERT(m_propertyReply);
        free(m_propertyReply);
        m_propertyReply = nullptr;
    }
}

void NetscapeUrlReceiver::set_data(char const* value, int length)
{
    auto origData = QByteArray::fromRawData(value, length);

    if (origData.indexOf('\n') == -1) {
        // there are no line breaks, not in Netscape url format or empty,
        // but try anyway
        set_data_internal(origData);
        return;
    }
    // remove every second line
    QByteArray data;
    int start = 0;
    bool remLine = false;
    while (start < length) {
        auto part = QByteArray::fromRawData(value + start, length - start);
        int const linebreak = part.indexOf('\n');
        if (linebreak == -1) {
            // no more linebreaks, end of work
            if (!remLine) {
                // append the rest
                data.append(part);
            }
            break;
        }
        if (remLine) {
            // no data to add, but add a linebreak for the next line
            data.append('\n');
        } else {
            // add data till before linebreak
            data.append(part.data(), linebreak);
        }
        remLine = !remLine;
        start = linebreak + 1;
    }
    set_data_internal(data);
}

void MozUrlReceiver::set_data(char const* value, int length)
{
    // represent as QByteArray (guaranteed '\0'-terminated)
    auto const origData = QByteArray::fromRawData(value, length);

    // text/x-moz-url data is sent in utf-16 - copies the content
    // and converts it into 8 byte representation
    auto const byteData
        = QString::fromUtf16(reinterpret_cast<char16_t const*>(origData.data())).toLatin1();

    if (byteData.indexOf('\n') == -1) {
        // there are no line breaks, not in text/x-moz-url format or empty,
        // but try anyway
        set_data_internal(byteData);
        return;
    }
    // remove every second line
    QByteArray data;
    auto start = 0;
    auto remLine = false;
    while (start < length) {
        auto part = QByteArray::fromRawData(byteData.data() + start, byteData.size() - start);
        int const linebreak = part.indexOf('\n');
        if (linebreak == -1) {
            // no more linebreaks, end of work
            if (!remLine) {
                // append the rest
                data.append(part);
            }
            break;
        }
        if (remLine) {
            // no data to add, but add a linebreak for the next line
            data.append('\n');
        } else {
            // add data till before linebreak
            data.append(part.data(), linebreak);
        }
        remLine = !remLine;
        start = linebreak + 1;
    }
    set_data_internal(data);
}

void TransferXtoWl::data_source_write()
{
    QByteArray property = m_receiver->data();

    auto len = write(fd(), property.constData(), property.size());
    if (len == -1) {
        qCWarning(KWIN_XWL) << "X11 to Wayland write error on fd:" << fd();
        end_transfer();
        return;
    }

    m_receiver->part_read(len);
    if (len == property.size()) {
        // property completely transferred
        if (incr()) {
            clear_socket_notifier();
            auto xcbConn = kwinApp()->x11Connection();
            xcb_delete_property(xcbConn, m_window, atoms->wl_selection);
            xcb_flush(xcbConn);
        } else {
            // transfer complete
            end_transfer();
        }
    } else {
        if (!socket_notifier()) {
            create_socket_notifier(QSocketNotifier::Write);
            connect(socket_notifier(), &QSocketNotifier::activated, this, [this](int socket) {
                Q_UNUSED(socket);
                data_source_write();
            });
        }
    }
    reset_timeout();
}

}
