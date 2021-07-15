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
    xcb_xfixes_select_selection_input(kwinApp()->x11Connection(), sel->window(), sel->atom(), mask);
    xcb_flush(xcb_conn);
}

// on selection owner changes by X clients (Xwl -> Wl)
template<typename Selection>
bool handle_xfixes_notify(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    if (event->window != sel->window()) {
        return false;
    }
    if (event->selection != sel->atom()) {
        return false;
    }
    if (sel->m_disownPending) {
        // notify of our own disown - ignore it
        sel->m_disownPending = false;
        return true;
    }
    if (event->owner == sel->window() && sel->wlSource()) {
        // When we claim a selection we must use XCB_TIME_CURRENT,
        // grab the actual timestamp here to answer TIMESTAMP requests
        // correctly
        sel->wlSource()->setTimestamp(event->timestamp);
        sel->m_timestamp = event->timestamp;
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
    if (event->selection != sel->atom()) {
        return false;
    }

    if (qobject_cast<win::x11::window*>(workspace()->activeClient()) == nullptr) {
        // Receiving Wayland selection not allowed when no Xwayland surface active
        // filter the event, but don't act upon it
        sendSelectionNotify(event, false);
        return true;
    }

    if (sel->window() != event->owner || !sel->m_waylandSource) {
        if (event->time < sel->m_timestamp) {
            // cancel earlier attempts at receiving a selection
            // TODO: is this for sure without problems?
            sendSelectionNotify(event, false);
            return true;
        }
        return false;
    }
    return sel->m_waylandSource->handleSelectionRequest(event);
}

template<typename Selection>
bool handle_selection_notify(Selection* sel, xcb_selection_notify_event_t* event)
{
    if (sel->m_xSource && event->requestor == sel->requestorWindow()
        && event->selection == sel->atom()) {
        if (sel->m_xSource->handleSelectionNotify(event)) {
            return true;
        }
    }
    for (auto& transfer : sel->m_xToWlTransfers) {
        if (transfer->handleSelectionNotify(event)) {
            return true;
        }
    }
    return false;
}

template<typename Selection>
bool handle_property_notify(Selection* sel, xcb_property_notify_event_t* event)
{
    for (auto& transfer : sel->m_xToWlTransfers) {
        if (transfer->handlePropertyNotify(event)) {
            return true;
        }
    }
    for (auto& transfer : sel->m_wlToXTransfers) {
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
        xcb_set_selection_owner(xcb_conn, sel->window(), sel->atom(), XCB_TIME_CURRENT_TIME);
    } else {
        sel->m_disownPending = true;
        xcb_set_selection_owner(xcb_conn, XCB_WINDOW_NONE, sel->atom(), sel->m_timestamp);
    }
    xcb_flush(xcb_conn);
}

template<typename Selection>
void overwrite_requestor_window(Selection* sel, xcb_window_t window)
{
    assert(sel->x11Source());
    sel->m_requestorWindow = window == XCB_WINDOW_NONE ? sel->window() : window;
}

using srv_data_device = Wrapland::Server::DataDevice;
using srv_data_source = Wrapland::Server::DataSource;
using clt_data_source = Wrapland::Client::DataSource;

// sets the current provider of the selection
template<typename Selection>
void set_wl_source(Selection* sel, WlSource<srv_data_device, srv_data_source>* source)
{
    delete sel->m_waylandSource;
    delete sel->m_xSource;
    sel->m_waylandSource = nullptr;
    sel->m_xSource = nullptr;
    if (source) {
        sel->m_waylandSource = source;
        QObject::connect(source->qobject(),
                         &qWlSource::transferReady,
                         sel->qobject(),
                         [sel](auto event, auto fd) { start_transfer_to_x11(sel, event, fd); });
    }
}

template<typename Selection>
void create_x11_source(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    delete sel->m_waylandSource;
    delete sel->m_xSource;
    sel->m_waylandSource = nullptr;
    sel->m_xSource = nullptr;
    if (!event || event->owner == XCB_WINDOW_NONE) {
        return;
    }
    sel->m_xSource = new X11Source<clt_data_source>(event);

    QObject::connect(
        sel->m_xSource->qobject(),
        &qX11Source::offersChanged,
        sel->qobject(),
        [sel](auto const& added, auto const& removed) { sel->x11OffersChanged(added, removed); });
    QObject::connect(sel->m_xSource->qobject(),
                     &qX11Source::transferReady,
                     sel->qobject(),
                     [sel](auto target, auto fd) { start_transfer_to_wayland(sel, target, fd); });
}

template<typename Selection>
void start_transfer_to_wayland(Selection* sel, xcb_atom_t target, qint32 fd)
{
    // create new x to wl data transfer object
    auto transfer = new TransferXtoWl(sel->atom(),
                                      target,
                                      fd,
                                      sel->m_xSource->timestamp(),
                                      sel->requestorWindow(),
                                      sel->qobject());
    sel->m_xToWlTransfers << transfer;

    QObject::connect(transfer, &TransferXtoWl::finished, sel->qobject(), [sel, transfer]() {
        Q_EMIT sel->qobject()->transferFinished(transfer->timestamp());
        delete transfer;
        sel->m_xToWlTransfers.removeOne(transfer);
        end_timeout_transfers_timer(sel);
    });
    start_timeout_transfers_timer(sel);
}

template<typename Selection>
void start_transfer_to_x11(Selection* sel, xcb_selection_request_event_t* event, qint32 fd)
{
    // create new wl to x data transfer object
    auto transfer = new TransferWltoX(sel->atom(), event, fd, sel->qobject());

    QObject::connect(
        transfer, &TransferWltoX::selectionNotify, sel->qobject(), &sendSelectionNotify);
    QObject::connect(transfer, &TransferWltoX::finished, sel->qobject(), [sel, transfer]() {
        Q_EMIT sel->qobject()->transferFinished(transfer->timestamp());

        // TODO: serialize? see comment below.
        //        const bool wasActive = (transfer == m_wlToXTransfers[0]);
        delete transfer;
        sel->m_wlToXTransfers.removeOne(transfer);
        end_timeout_transfers_timer(sel);
        //        if (wasActive && !m_wlToXTransfers.isEmpty()) {
        //            m_wlToXTransfers[0]->startTransferFromSource();
        //        }
    });

    // add it to list of queued transfers
    sel->m_wlToXTransfers.append(transfer);

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
    for (auto& transfer : sel->m_xToWlTransfers) {
        transfer->timeout();
    }
    for (auto& transfer : sel->m_wlToXTransfers) {
        transfer->timeout();
    }
}

template<typename Selection>
void start_timeout_transfers_timer(Selection* sel)
{
    if (sel->m_timeoutTransfers) {
        return;
    }
    sel->m_timeoutTransfers = new QTimer(sel->qobject());
    QObject::connect(sel->m_timeoutTransfers, &QTimer::timeout, sel->qobject(), [sel]() {
        timeout_transfers(sel);
    });
    sel->m_timeoutTransfers->start(5000);
}

template<typename Selection>
void end_timeout_transfers_timer(Selection* sel)
{
    if (sel->m_xToWlTransfers.isEmpty() && sel->m_wlToXTransfers.isEmpty()) {
        delete sel->m_timeoutTransfers;
        sel->m_timeoutTransfers = nullptr;
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
