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
#include "selection.h"
#include "databridge.h"
#include "selection_source.h"
#include "selection_utils.h"
#include "transfer.h"

#include "atoms.h"
#include "workspace.h"

#include "win/x11/window.h"

#include <xcb/xcb_event.h>
#include <xcb/xfixes.h>

#include <QTimer>

namespace KWin
{
namespace Xwl
{

Selection::Selection(xcb_atom_t atom)
    : m_qobject(new q_selection)
    , m_atom(atom)
{
    xcb_connection_t* xcbConn = kwinApp()->x11Connection();
    m_window = xcb_generate_id(kwinApp()->x11Connection());
    m_requestorWindow = m_window;
    xcb_flush(xcbConn);
}

Selection::~Selection()
{
    delete m_waylandSource;
    delete m_xSource;
    m_waylandSource = nullptr;
    m_xSource = nullptr;
}

bool Selection::handleXfixesNotify(xcb_xfixes_selection_notify_event_t* event)
{
    if (event->window != m_window) {
        return false;
    }
    if (event->selection != m_atom) {
        return false;
    }
    if (m_disownPending) {
        // notify of our own disown - ignore it
        m_disownPending = false;
        return true;
    }
    if (event->owner == m_window && m_waylandSource) {
        // When we claim a selection we must use XCB_TIME_CURRENT,
        // grab the actual timestamp here to answer TIMESTAMP requests
        // correctly
        m_waylandSource->setTimestamp(event->timestamp);
        m_timestamp = event->timestamp;
        return true;
    }

    // Being here means some other X window has claimed the selection.
    doHandleXfixesNotify(event);
    return true;
}

bool Selection::filterEvent(xcb_generic_event_t* event)
{
    switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
    case XCB_SELECTION_NOTIFY:
        return handleSelectionNotify(reinterpret_cast<xcb_selection_notify_event_t*>(event));
    case XCB_PROPERTY_NOTIFY:
        return handlePropertyNotify(reinterpret_cast<xcb_property_notify_event_t*>(event));
    case XCB_SELECTION_REQUEST:
        return handleSelectionRequest(reinterpret_cast<xcb_selection_request_event_t*>(event));
    case XCB_CLIENT_MESSAGE:
        return handleClientMessage(reinterpret_cast<xcb_client_message_event_t*>(event));
    default:
        return false;
    }
}

void Selection::registerXfixes()
{
    xcb_connection_t* xcbConn = kwinApp()->x11Connection();
    const uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
    xcb_xfixes_select_selection_input(kwinApp()->x11Connection(), m_window, m_atom, mask);
    xcb_flush(xcbConn);
}

void Selection::setWlSource(WlSource<srv_data_device, srv_data_source>* source)
{
    delete m_waylandSource;
    delete m_xSource;
    m_waylandSource = nullptr;
    m_xSource = nullptr;
    if (source) {
        m_waylandSource = source;
        QObject::connect(source->qobject(),
                         &qWlSource::transferReady,
                         qobject(),
                         [this](auto event, auto fd) { startTransferToX(event, fd); });
    }
}

void Selection::createX11Source(xcb_xfixes_selection_notify_event_t* event)
{
    delete m_waylandSource;
    delete m_xSource;
    m_waylandSource = nullptr;
    m_xSource = nullptr;
    if (!event || event->owner == XCB_WINDOW_NONE) {
        return;
    }
    m_xSource = new X11Source<clt_data_source>(event);

    QObject::connect(
        m_xSource->qobject(),
        &qX11Source::offersChanged,
        qobject(),
        [this](auto const& added, auto const& removed) { x11OffersChanged(added, removed); });
    QObject::connect(m_xSource->qobject(),
                     &qX11Source::transferReady,
                     qobject(),
                     [this](auto target, auto fd) { startTransferToWayland(target, fd); });
}

void Selection::ownSelection(bool own)
{
    xcb_connection_t* xcbConn = kwinApp()->x11Connection();
    if (own) {
        xcb_set_selection_owner(xcbConn, m_window, m_atom, XCB_TIME_CURRENT_TIME);
    } else {
        m_disownPending = true;
        xcb_set_selection_owner(xcbConn, XCB_WINDOW_NONE, m_atom, m_timestamp);
    }
    xcb_flush(xcbConn);
}

void Selection::overwriteRequestorWindow(xcb_window_t window)
{
    Q_ASSERT(m_xSource);
    m_requestorWindow = window == XCB_WINDOW_NONE ? m_window : window;
}

bool Selection::handleSelectionRequest(xcb_selection_request_event_t* event)
{
    if (event->selection != m_atom) {
        return false;
    }

    if (qobject_cast<win::x11::window*>(workspace()->activeClient()) == nullptr) {
        // Receiving Wayland selection not allowed when no Xwayland surface active
        // filter the event, but don't act upon it
        sendSelectionNotify(event, false);
        return true;
    }

    if (m_window != event->owner || !m_waylandSource) {
        if (event->time < m_timestamp) {
            // cancel earlier attempts at receiving a selection
            // TODO: is this for sure without problems?
            sendSelectionNotify(event, false);
            return true;
        }
        return false;
    }
    return m_waylandSource->handleSelectionRequest(event);
}

bool Selection::handleSelectionNotify(xcb_selection_notify_event_t* event)
{
    if (m_xSource && event->requestor == m_requestorWindow && event->selection == m_atom) {
        if (m_xSource->handleSelectionNotify(event)) {
            return true;
        }
    }
    for (TransferXtoWl* transfer : m_xToWlTransfers) {
        if (transfer->handleSelectionNotify(event)) {
            return true;
        }
    }
    return false;
}

bool Selection::handlePropertyNotify(xcb_property_notify_event_t* event)
{
    for (TransferXtoWl* transfer : m_xToWlTransfers) {
        if (transfer->handlePropertyNotify(event)) {
            return true;
        }
    }
    for (TransferWltoX* transfer : m_wlToXTransfers) {
        if (transfer->handlePropertyNotify(event)) {
            return true;
        }
    }
    return false;
}

void Selection::startTransferToWayland(xcb_atom_t target, qint32 fd)
{
    // create new x to wl data transfer object
    auto* transfer = new TransferXtoWl(
        m_atom, target, fd, m_xSource->timestamp(), m_requestorWindow, qobject());
    m_xToWlTransfers << transfer;

    QObject::connect(transfer, &TransferXtoWl::finished, qobject(), [this, transfer]() {
        Q_EMIT qobject()->transferFinished(transfer->timestamp());
        delete transfer;
        m_xToWlTransfers.removeOne(transfer);
        endTimeoutTransfersTimer();
    });
    startTimeoutTransfersTimer();
}

void Selection::startTransferToX(xcb_selection_request_event_t* event, qint32 fd)
{
    // create new wl to x data transfer object
    auto* transfer = new TransferWltoX(m_atom, event, fd, qobject());

    QObject::connect(transfer, &TransferWltoX::selectionNotify, qobject(), &sendSelectionNotify);
    QObject::connect(transfer, &TransferWltoX::finished, qobject(), [this, transfer]() {
        Q_EMIT qobject()->transferFinished(transfer->timestamp());

        // TODO: serialize? see comment below.
        //        const bool wasActive = (transfer == m_wlToXTransfers[0]);
        delete transfer;
        m_wlToXTransfers.removeOne(transfer);
        endTimeoutTransfersTimer();
        //        if (wasActive && !m_wlToXTransfers.isEmpty()) {
        //            m_wlToXTransfers[0]->startTransferFromSource();
        //        }
    });

    // add it to list of queued transfers
    m_wlToXTransfers.append(transfer);

    // TODO: Do we need to serialize the transfers, or can we do
    //       them in parallel as we do it right now?
    transfer->startTransferFromSource();
    //    if (m_wlToXTransfers.size() == 1) {
    //        transfer->startTransferFromSource();
    //    }
    startTimeoutTransfersTimer();
}

void Selection::startTimeoutTransfersTimer()
{
    if (m_timeoutTransfers) {
        return;
    }
    m_timeoutTransfers = new QTimer(qobject());
    QObject::connect(
        m_timeoutTransfers, &QTimer::timeout, qobject(), [this]() { timeoutTransfers(); });
    m_timeoutTransfers->start(5000);
}

void Selection::endTimeoutTransfersTimer()
{
    if (m_xToWlTransfers.isEmpty() && m_wlToXTransfers.isEmpty()) {
        delete m_timeoutTransfers;
        m_timeoutTransfers = nullptr;
    }
}

void Selection::timeoutTransfers()
{
    for (TransferXtoWl* transfer : m_xToWlTransfers) {
        transfer->timeout();
    }
    for (TransferWltoX* transfer : m_wlToXTransfers) {
        transfer->timeout();
    }
}

} // namespace Xwl
} // namespace KWin
