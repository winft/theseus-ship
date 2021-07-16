/*
    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection_source.h"
#include "transfer.h"

#include "atoms.h"
#include "win/x11/window.h"
#include "workspace.h"

#include <QString>
#include <QStringList>
#include <QTimer>

#include <xcb/xcb_event.h>
#include <xcb/xfixes.h>
#include <xcbutils.h>

namespace Wrapland::Server
{
class DataDevice;
class DataSource;
}
namespace Wrapland::Client
{
class DataSource;
}

namespace KWin::Xwl
{

inline void sendSelectionNotify(xcb_selection_request_event_t* event, bool success)
{
    xcb_selection_notify_event_t notify;
    notify.response_type = XCB_SELECTION_NOTIFY;
    notify.sequence = 0;
    notify.time = event->time;
    notify.requestor = event->requestor;
    notify.selection = event->selection;
    notify.target = event->target;
    notify.property = success ? event->property : xcb_atom_t(XCB_ATOM_NONE);

    xcb_connection_t* xcbConn = kwinApp()->x11Connection();
    xcb_send_event(xcbConn, 0, event->requestor, XCB_EVENT_MASK_NO_EVENT, (const char*)&notify);
    xcb_flush(xcbConn);
}

template<typename Selection>
void register_xfixes(Selection* sel)
{
    auto xcb_conn = kwinApp()->x11Connection();
    const uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
    xcb_xfixes_select_selection_input(kwinApp()->x11Connection(), sel->window, sel->atom, mask);
    xcb_flush(xcb_conn);
}

// on selection owner changes by X clients (Xwl -> Wl)
template<typename Selection>
bool handle_xfixes_notify(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    if (event->window != sel->window) {
        return false;
    }
    if (event->selection != sel->atom) {
        return false;
    }
    if (sel->disown_pending) {
        // notify of our own disown - ignore it
        sel->disown_pending = false;
        return true;
    }
    if (event->owner == sel->window && sel->wayland_source) {
        // When we claim a selection we must use XCB_TIME_CURRENT,
        // grab the actual timestamp here to answer TIMESTAMP requests
        // correctly
        sel->wayland_source->setTimestamp(event->timestamp);
        sel->timestamp = event->timestamp;
        return true;
    }

    // Being here means some other X window has claimed the selection.
    sel->doHandleXfixesNotify(event);
    return true;
}

template<typename Selection>
bool filter_event(Selection* sel, xcb_generic_event_t* event)
{
    switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
    case XCB_SELECTION_NOTIFY:
        return handle_selection_notify(sel, reinterpret_cast<xcb_selection_notify_event_t*>(event));
    case XCB_PROPERTY_NOTIFY:
        return handle_property_notify(sel, reinterpret_cast<xcb_property_notify_event_t*>(event));
    case XCB_SELECTION_REQUEST:
        return handle_selection_request(sel,
                                        reinterpret_cast<xcb_selection_request_event_t*>(event));
    case XCB_CLIENT_MESSAGE:
        return sel->handleClientMessage(reinterpret_cast<xcb_client_message_event_t*>(event));
    default:
        return false;
    }
}

template<typename Selection>
bool handle_selection_request(Selection* sel, xcb_selection_request_event_t* event)
{
    if (event->selection != sel->atom) {
        return false;
    }

    if (qobject_cast<win::x11::window*>(workspace()->activeClient()) == nullptr) {
        // Receiving Wayland selection not allowed when no Xwayland surface active
        // filter the event, but don't act upon it
        sendSelectionNotify(event, false);
        return true;
    }

    if (sel->window != event->owner || !sel->wayland_source) {
        if (event->time < sel->timestamp) {
            // cancel earlier attempts at receiving a selection
            // TODO: is this for sure without problems?
            sendSelectionNotify(event, false);
            return true;
        }
        return false;
    }
    return sel->wayland_source->handleSelectionRequest(event);
}

template<typename Selection>
bool handle_selection_notify(Selection* sel, xcb_selection_notify_event_t* event)
{
    if (sel->x11_source && event->requestor == sel->requestor_window
        && event->selection == sel->atom) {
        if (sel->x11_source->handleSelectionNotify(event)) {
            return true;
        }
    }
    for (auto& transfer : sel->transfers.x11_to_wl) {
        if (transfer->handleSelectionNotify(event)) {
            return true;
        }
    }
    return false;
}

template<typename Selection>
bool handle_property_notify(Selection* sel, xcb_property_notify_event_t* event)
{
    for (auto& transfer : sel->transfers.x11_to_wl) {
        if (transfer->handlePropertyNotify(event)) {
            return true;
        }
    }
    for (auto& transfer : sel->transfers.wl_to_x11) {
        if (transfer->handlePropertyNotify(event)) {
            return true;
        }
    }
    return false;
}

// must be called in order to provide data from Wl to X
template<typename Selection>
void own_selection(Selection* sel, bool own)
{
    auto xcb_conn = kwinApp()->x11Connection();
    if (own) {
        xcb_set_selection_owner(xcb_conn, sel->window, sel->atom, XCB_TIME_CURRENT_TIME);
    } else {
        sel->disown_pending = true;
        xcb_set_selection_owner(xcb_conn, XCB_WINDOW_NONE, sel->atom, sel->timestamp);
    }
    xcb_flush(xcb_conn);
}

template<typename Selection>
void overwrite_requestor_window(Selection* sel, xcb_window_t window)
{
    assert(sel->x11_source);
    sel->requestor_window = window == XCB_WINDOW_NONE ? sel->window : window;
}

using srv_data_device = Wrapland::Server::DataDevice;
using srv_data_source = Wrapland::Server::DataSource;
using clt_data_source = Wrapland::Client::DataSource;

// sets the current provider of the selection
template<typename Selection>
void set_wl_source(Selection* sel, WlSource<srv_data_device, srv_data_source>* source)
{
    delete sel->wayland_source;
    delete sel->x11_source;
    sel->wayland_source = nullptr;
    sel->x11_source = nullptr;
    if (source) {
        sel->wayland_source = source;
        QObject::connect(source->qobject(),
                         &qWlSource::transferReady,
                         sel->qobject.get(),
                         [sel](auto event, auto fd) { start_transfer_to_x11(sel, event, fd); });
    }
}

template<typename Selection>
void create_x11_source(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    delete sel->wayland_source;
    delete sel->x11_source;
    sel->wayland_source = nullptr;
    sel->x11_source = nullptr;
    if (!event || event->owner == XCB_WINDOW_NONE) {
        return;
    }
    sel->x11_source = new X11Source<clt_data_source>(event);

    QObject::connect(
        sel->x11_source->qobject(),
        &qX11Source::offersChanged,
        sel->qobject.get(),
        [sel](auto const& added, auto const& removed) { sel->x11OffersChanged(added, removed); });
    QObject::connect(sel->x11_source->qobject(),
                     &qX11Source::transferReady,
                     sel->qobject.get(),
                     [sel](auto target, auto fd) { start_transfer_to_wayland(sel, target, fd); });
}

template<typename Selection>
void start_transfer_to_wayland(Selection* sel, xcb_atom_t target, qint32 fd)
{
    // create new x to wl data transfer object
    auto transfer = new TransferXtoWl(sel->atom,
                                      target,
                                      fd,
                                      sel->x11_source->timestamp(),
                                      sel->requestor_window,
                                      sel->qobject.get());
    sel->transfers.x11_to_wl << transfer;

    QObject::connect(transfer, &TransferXtoWl::finished, sel->qobject.get(), [sel, transfer]() {
        Q_EMIT sel->qobject->transferFinished(transfer->timestamp());
        delete transfer;
        sel->transfers.x11_to_wl.removeOne(transfer);
        end_timeout_transfers_timer(sel);
    });
    start_timeout_transfers_timer(sel);
}

template<typename Selection>
void start_transfer_to_x11(Selection* sel, xcb_selection_request_event_t* event, qint32 fd)
{
    // create new wl to x data transfer object
    auto transfer = new TransferWltoX(sel->atom, event, fd, sel->qobject.get());

    QObject::connect(
        transfer, &TransferWltoX::selectionNotify, sel->qobject.get(), &sendSelectionNotify);
    QObject::connect(transfer, &TransferWltoX::finished, sel->qobject.get(), [sel, transfer]() {
        Q_EMIT sel->qobject->transferFinished(transfer->timestamp());

        // TODO: serialize? see comment below.
        //        const bool wasActive = (transfer == m_wlToXTransfers[0]);
        delete transfer;
        sel->transfers.wl_to_x11.removeOne(transfer);
        end_timeout_transfers_timer(sel);
        //        if (wasActive && !m_wlToXTransfers.isEmpty()) {
        //            m_wlToXTransfers[0]->startTransferFromSource();
        //        }
    });

    // add it to list of queued transfers
    sel->transfers.wl_to_x11.append(transfer);

    // TODO: Do we need to serialize the transfers, or can we do
    //       them in parallel as we do it right now?
    transfer->startTransferFromSource();
    //    if (m_wlToXTransfers.size() == 1) {
    //        transfer->startTransferFromSource();
    //    }
    start_timeout_transfers_timer(sel);
}

// Timeout transfers, which have become inactive due to client errors.
template<typename Selection>
void timeout_transfers(Selection* sel)
{
    for (auto& transfer : sel->transfers.x11_to_wl) {
        transfer->timeout();
    }
    for (auto& transfer : sel->transfers.wl_to_x11) {
        transfer->timeout();
    }
}

template<typename Selection>
void start_timeout_transfers_timer(Selection* sel)
{
    if (sel->transfers.timeout) {
        return;
    }
    sel->transfers.timeout = new QTimer(sel->qobject.get());
    QObject::connect(sel->transfers.timeout, &QTimer::timeout, sel->qobject.get(), [sel]() {
        timeout_transfers(sel);
    });
    sel->transfers.timeout->start(5000);
}

template<typename Selection>
void end_timeout_transfers_timer(Selection* sel)
{
    if (sel->transfers.x11_to_wl.isEmpty() && sel->transfers.wl_to_x11.isEmpty()) {
        delete sel->transfers.timeout;
        sel->transfers.timeout = nullptr;
    }
}

inline xcb_atom_t mimeTypeToAtomLiteral(const QString& mimeType)
{
    return Xcb::Atom(mimeType.toLatin1(), false, kwinApp()->x11Connection());
}

inline xcb_atom_t mimeTypeToAtom(const QString& mimeType)
{
    if (mimeType == QLatin1String("text/plain;charset=utf-8")) {
        return atoms->utf8_string;
    }
    if (mimeType == QLatin1String("text/plain")) {
        return atoms->text;
    }
    if (mimeType == QLatin1String("text/x-uri")) {
        return atoms->uri_list;
    }
    return mimeTypeToAtomLiteral(mimeType);
}

inline QString atomName(xcb_atom_t atom)
{
    xcb_connection_t* xcbConn = kwinApp()->x11Connection();
    xcb_get_atom_name_cookie_t nameCookie = xcb_get_atom_name(xcbConn, atom);
    xcb_get_atom_name_reply_t* nameReply = xcb_get_atom_name_reply(xcbConn, nameCookie, nullptr);
    if (!nameReply) {
        return QString();
    }

    const size_t length = xcb_get_atom_name_name_length(nameReply);
    QString name = QString::fromLatin1(xcb_get_atom_name_name(nameReply), length);
    free(nameReply);
    return name;
}

inline QStringList atomToMimeTypes(xcb_atom_t atom)
{
    QStringList mimeTypes;

    if (atom == atoms->utf8_string) {
        mimeTypes << QString::fromLatin1("text/plain;charset=utf-8");
    } else if (atom == atoms->text) {
        mimeTypes << QString::fromLatin1("text/plain");
    } else if (atom == atoms->uri_list || atom == atoms->netscape_url || atom == atoms->moz_url) {
        // We identify netscape and moz format as less detailed formats text/uri-list,
        // text/x-uri and accept the information loss.
        mimeTypes << QString::fromLatin1("text/uri-list") << QString::fromLatin1("text/x-uri");
    } else {
        mimeTypes << atomName(atom);
    }
    return mimeTypes;
}

}
