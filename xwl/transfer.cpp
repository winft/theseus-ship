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

#include "main.h"

#include <unistd.h>
#include <xwayland_logging.h>

namespace KWin::xwl
{

// in Bytes: equals 64KB
constexpr uint32_t s_incrChunkSize = 63 * 1024;

transfer::transfer(xcb_atom_t selection,
                   qint32 fd,
                   xcb_timestamp_t timestamp,
                   base::x11::atoms const& atoms,
                   QObject* parent)
    : QObject(parent)
    , atoms{atoms}
    , atom{selection}
    , fd{fd}
    , timestamp{timestamp}
{
}

void transfer::create_socket_notifier(QSocketNotifier::Type type)
{
    delete notifier;
    notifier = new QSocketNotifier(fd, type, this);
}

void transfer::clear_socket_notifier()
{
    delete notifier;
    notifier = nullptr;
}

void transfer::timeout()
{
    if (timed_out) {
        end_transfer();
    }
    timed_out = true;
}

void transfer::end_transfer()
{
    clear_socket_notifier();
    close_fd();
    Q_EMIT finished();
}

void transfer::close_fd()
{
    if (fd < 0) {
        return;
    }
    close(fd);
    fd = -1;
}

wl_to_x11_transfer::wl_to_x11_transfer(xcb_atom_t selection,
                                       xcb_selection_request_event_t* request,
                                       qint32 fd,
                                       base::x11::atoms const& atoms,
                                       QObject* parent)
    : transfer(selection, fd, 0, atoms, parent)
    , request(request)
{
}

wl_to_x11_transfer::~wl_to_x11_transfer()
{
    delete request;
    request = nullptr;
}

void wl_to_x11_transfer::start_transfer_from_source()
{
    create_socket_notifier(QSocketNotifier::Read);
    connect(socket_notifier(), &QSocketNotifier::activated, this, [this](int socket) {
        Q_UNUSED(socket);
        read_wl_source();
    });
}

int wl_to_x11_transfer::flush_source_data()
{
    auto xcb_con = kwinApp()->x11Connection();

    xcb_change_property(xcb_con,
                        XCB_PROP_MODE_REPLACE,
                        request->requestor,
                        request->property,
                        request->target,
                        8,
                        chunks.front().first.size(),
                        chunks.front().first.data());
    xcb_flush(xcb_con);

    property_is_set = true;
    reset_timeout();

    auto const rm = chunks.front();
    chunks.pop_front();
    return rm.first.size();
}

void wl_to_x11_transfer::start_incr()
{
    Q_ASSERT(chunks.size() == 1);

    auto xcb_con = kwinApp()->x11Connection();

    uint32_t mask[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(xcb_con, request->requestor, XCB_CW_EVENT_MASK, mask);

    // spec says to make the available space larger
    uint32_t const chunkSpace = 1024 + s_incrChunkSize;
    xcb_change_property(xcb_con,
                        XCB_PROP_MODE_REPLACE,
                        request->requestor,
                        request->property,
                        atoms.incr,
                        32,
                        1,
                        &chunkSpace);
    xcb_flush(xcb_con);

    set_incr(true);
    // first data will be flushed after the property has been deleted
    // again by the requestor
    flush_property_on_delete = true;
    property_is_set = true;
    Q_EMIT selection_notify(request, true);
}

void wl_to_x11_transfer::read_wl_source()
{
    if (chunks.size() == 0 || chunks.back().second == s_incrChunkSize) {
        // append new chunk
        auto next = std::pair<QByteArray, int>();
        next.first.resize(s_incrChunkSize);
        next.second = 0;
        chunks.push_back(next);
    }

    auto const oldLen = chunks.back().second;
    auto const avail = s_incrChunkSize - chunks.back().second;
    Q_ASSERT(avail > 0);

    ssize_t readLen = read(get_fd(), chunks.back().first.data() + oldLen, avail);
    if (readLen == -1) {
        qCWarning(KWIN_XWL) << "Error reading in Wl data.";

        // TODO: cleanup X side?
        end_transfer();
        return;
    }
    chunks.back().second = oldLen + readLen;

    if (readLen == 0) {
        // at the fd end - complete transfer now
        chunks.back().first.resize(chunks.back().second);

        if (get_incr()) {
            // incremental transfer is to be completed now
            flush_property_on_delete = true;
            if (!property_is_set) {
                // flush if target's property is not set at the moment
                flush_source_data();
            }
            clear_socket_notifier();
        } else {
            // non incremental transfer is to be completed now,
            // data can be transferred to X client via a single property set
            flush_source_data();
            Q_EMIT selection_notify(request, true);
            end_transfer();
        }
    } else if (chunks.back().second == s_incrChunkSize) {
        // first chunk full, but not yet at fd end -> go incremental
        if (get_incr()) {
            flush_property_on_delete = true;
            if (!property_is_set) {
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

bool wl_to_x11_transfer::handle_property_notify(xcb_property_notify_event_t* event)
{
    if (event->window == request->requestor) {
        if (event->state == XCB_PROPERTY_DELETE && event->atom == request->property) {
            handle_property_delete();
        }
        return true;
    }
    return false;
}

void wl_to_x11_transfer::handle_property_delete()
{
    if (!get_incr()) {
        // non-incremental transfer: nothing to do
        return;
    }
    property_is_set = false;

    if (flush_property_on_delete) {
        if (!socket_notifier() && chunks.empty()) {
            // transfer complete
            auto xcb_con = kwinApp()->x11Connection();

            uint32_t mask[] = {0};
            xcb_change_window_attributes(xcb_con, request->requestor, XCB_CW_EVENT_MASK, mask);

            xcb_change_property(xcb_con,
                                XCB_PROP_MODE_REPLACE,
                                request->requestor,
                                request->property,
                                request->target,
                                8,
                                0,
                                nullptr);
            xcb_flush(xcb_con);
            flush_property_on_delete = false;
            end_transfer();
        } else {
            flush_source_data();
        }
    }
}

x11_to_wl_transfer::x11_to_wl_transfer(xcb_atom_t selection,
                                       xcb_atom_t target,
                                       qint32 fd,
                                       xcb_timestamp_t timestamp,
                                       xcb_window_t parentWindow,
                                       x11_data const& x11,
                                       QObject* parent)
    : transfer(selection, fd, timestamp, *x11.atoms, parent)
{
    // create transfer window
    auto xcb_con = kwinApp()->x11Connection();
    window = xcb_generate_id(xcb_con);
    uint32_t const values[] = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_create_window(xcb_con,
                      XCB_COPY_FROM_PARENT,
                      window,
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
    xcb_convert_selection(xcb_con, window, selection, target, atoms.wl_selection, timestamp);
    xcb_flush(xcb_con);
}

x11_to_wl_transfer::~x11_to_wl_transfer()
{
    auto xcb_con = kwinApp()->x11Connection();
    xcb_destroy_window(xcb_con, window);
    xcb_flush(xcb_con);

    delete receiver;
    receiver = nullptr;
}

bool x11_to_wl_transfer::handle_property_notify(xcb_property_notify_event_t* event)
{
    if (event->window == window) {
        if (event->state == XCB_PROPERTY_NEW_VALUE && event->atom == atoms.wl_selection) {
            get_incr_chunk();
        }
        return true;
    }
    return false;
}

bool x11_to_wl_transfer::handle_selection_notify(xcb_selection_notify_event_t* event)
{
    if (event->requestor != window) {
        return false;
    }
    if (event->selection != get_atom()) {
        return false;
    }
    if (event->property == XCB_ATOM_NONE) {
        qCWarning(KWIN_XWL) << "Incoming X selection conversion failed";
        return true;
    }
    if (event->target == atoms.targets) {
        qCWarning(KWIN_XWL) << "Received targets too late";
        // TODO: or allow it?
        return true;
    }
    if (receiver) {
        // second selection notify element - misbehaving source

        // TODO: cancel this transfer?
        return true;
    }

    if (event->target == atoms.netscape_url) {
        receiver = new netscape_url_receiver;
    } else if (event->target == atoms.moz_url) {
        receiver = new moz_url_receiver;
    } else {
        receiver = new data_receiver;
    }
    start_transfer();
    return true;
}

void x11_to_wl_transfer::start_transfer()
{
    auto xcb_con = kwinApp()->x11Connection();
    auto cookie = xcb_get_property(
        xcb_con, 1, window, atoms.wl_selection, XCB_GET_PROPERTY_TYPE_ANY, 0, 0x1fffffff);

    auto reply = xcb_get_property_reply(xcb_con, cookie, nullptr);
    if (reply == nullptr) {
        qCWarning(KWIN_XWL) << "Can't get selection property.";
        end_transfer();
        return;
    }

    if (reply->type == atoms.incr) {
        set_incr(true);
        free(reply);
    } else {
        set_incr(false);
        // reply's ownership is transferred
        receiver->transfer_from_property(reply);
        data_source_write();
    }
}

void x11_to_wl_transfer::get_incr_chunk()
{
    if (!get_incr()) {
        // source tries to sent incrementally, but did not announce it before
        return;
    }
    if (!receiver) {
        // receive mechanism has not yet been setup
        return;
    }
    auto xcb_con = kwinApp()->x11Connection();

    auto cookie = xcb_get_property(
        xcb_con, 0, window, atoms.wl_selection, XCB_GET_PROPERTY_TYPE_ANY, 0, 0x1fffffff);

    auto reply = xcb_get_property_reply(xcb_con, cookie, nullptr);
    if (!reply) {
        qCWarning(KWIN_XWL) << "Can't get selection property.";
        end_transfer();
        return;
    }

    if (xcb_get_property_value_length(reply) > 0) {
        // reply's ownership is transferred
        receiver->transfer_from_property(reply);
        data_source_write();
    } else {
        // transfer complete
        free(reply);
        end_transfer();
    }
}

data_receiver::~data_receiver()
{
    if (property_reply) {
        free(property_reply);
        property_reply = nullptr;
    }
}

void data_receiver::transfer_from_property(xcb_get_property_reply_t* reply)
{
    property_start = 0;
    property_reply = reply;

    set_data(static_cast<char*>(xcb_get_property_value(reply)),
             xcb_get_property_value_length(reply));
}

void data_receiver::set_data(char const* value, int length)
{
    // simply set data without copy
    data = QByteArray::fromRawData(value, length);
}

QByteArray data_receiver::get_data() const
{
    return QByteArray::fromRawData(data.data() + property_start, data.size() - property_start);
}

void data_receiver::part_read(int length)
{
    property_start += length;
    if (property_start == data.size()) {
        Q_ASSERT(property_reply);
        free(property_reply);
        property_reply = nullptr;
    }
}

void netscape_url_receiver::set_data(char const* value, int length)
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

void moz_url_receiver::set_data(char const* value, int length)
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

void x11_to_wl_transfer::data_source_write()
{
    auto property = receiver->get_data();

    auto len = write(get_fd(), property.constData(), property.size());
    if (len == -1) {
        qCWarning(KWIN_XWL) << "X11 to Wayland write error on fd:" << get_fd();
        end_transfer();
        return;
    }

    receiver->part_read(len);
    if (len == property.size()) {
        // property completely transferred
        if (get_incr()) {
            clear_socket_notifier();
            auto xcb_con = kwinApp()->x11Connection();
            xcb_delete_property(xcb_con, window, atoms.wl_selection);
            xcb_flush(xcb_con);
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
