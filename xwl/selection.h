/*
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection_source.h"
#include "transfer.h"

#include "atoms.h"
#include "win/x11/window.h"
#include "workspace.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xfixes.h>
#include <xcbutils.h>

#include <memory>

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
class TransferWltoX;
class TransferXtoWl;
template<typename, typename>
class WlSource;
template<typename>
class X11Source;

using srv_data_device = Wrapland::Server::DataDevice;
using srv_data_source = Wrapland::Server::DataSource;
using clt_data_source = Wrapland::Client::DataSource;

/*
 * QObject attribute of a Selection.
 * This is a hack around having a template QObject.
 */
class q_selection : public QObject
{
    Q_OBJECT

public:
Q_SIGNALS:
    void transferFinished(xcb_timestamp_t eventTime);
};

/**
 * Base class representing generic X selections and their respective
 * Wayland counter-parts.
 *
 * The class needs to be subclassed and adjusted according to the
 * selection, but provides common fucntionality to be expected of all
 * selections.
 *
 * A selection should exist through the whole runtime of an Xwayland
 * session.
 *
 * Independently of each other the class holds the currently active
 * source instance and active transfers relative to the represented
 * selection.
 */
class selection_data
{
public:
    std::unique_ptr<q_selection> qobject;

    xcb_atom_t atom{XCB_ATOM_NONE};
    xcb_window_t window{XCB_WINDOW_NONE};

    bool disown_pending{false};
    xcb_timestamp_t timestamp;
    xcb_window_t requestor_window{XCB_WINDOW_NONE};

    // Active source, if any. Only one of them at max can exist
    // at the same time.
    WlSource<srv_data_device, srv_data_source>* wayland_source{nullptr};
    X11Source<clt_data_source>* x11_source{nullptr};

    // active transfers
    struct {
        QVector<TransferWltoX*> wl_to_x11;
        QVector<TransferXtoWl*> x11_to_wl;
        QTimer* timeout{nullptr};
    } transfers;

    selection_data() = default;
    selection_data(selection_data const&) = delete;
    selection_data& operator=(selection_data const&) = delete;
    selection_data(selection_data&&) noexcept = default;
    selection_data& operator=(selection_data&&) noexcept = default;

    ~selection_data()
    {
        delete wayland_source;
        delete x11_source;
        wayland_source = nullptr;
        x11_source = nullptr;
    }
};

inline selection_data create_selection_data(xcb_atom_t atom)
{
    selection_data sel;

    sel.qobject.reset(new q_selection());
    sel.atom = atom;

    auto xcb_con = kwinApp()->x11Connection();
    sel.window = xcb_generate_id(kwinApp()->x11Connection());
    sel.requestor_window = sel.window;
    xcb_flush(xcb_con);

    return sel;
}

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
    xcb_xfixes_select_selection_input(
        kwinApp()->x11Connection(), sel->data.window, sel->data.atom, mask);
    xcb_flush(xcb_conn);
}

// on selection owner changes by X clients (Xwl -> Wl)
template<typename Selection>
bool handle_xfixes_notify(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    if (!sel) {
        return false;
    }
    if (event->window != sel->data.window) {
        return false;
    }
    if (event->selection != sel->data.atom) {
        return false;
    }
    if (sel->data.disown_pending) {
        // notify of our own disown - ignore it
        sel->data.disown_pending = false;
        return true;
    }
    if (event->owner == sel->data.window && sel->data.wayland_source) {
        // When we claim a selection we must use XCB_TIME_CURRENT,
        // grab the actual timestamp here to answer TIMESTAMP requests
        // correctly
        sel->data.wayland_source->setTimestamp(event->timestamp);
        sel->data.timestamp = event->timestamp;
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
    if (event->selection != sel->data.atom) {
        return false;
    }

    if (qobject_cast<win::x11::window*>(workspace()->activeClient()) == nullptr) {
        // Receiving Wayland selection not allowed when no Xwayland surface active
        // filter the event, but don't act upon it
        sendSelectionNotify(event, false);
        return true;
    }

    if (sel->data.window != event->owner || !sel->data.wayland_source) {
        if (event->time < sel->data.timestamp) {
            // cancel earlier attempts at receiving a selection
            // TODO: is this for sure without problems?
            sendSelectionNotify(event, false);
            return true;
        }
        return false;
    }
    return sel->data.wayland_source->handleSelectionRequest(event);
}

template<typename Selection>
bool handle_selection_notify(Selection* sel, xcb_selection_notify_event_t* event)
{
    if (sel->data.x11_source && event->requestor == sel->data.requestor_window
        && event->selection == sel->data.atom) {
        if (sel->data.x11_source->handleSelectionNotify(event)) {
            return true;
        }
    }
    for (auto& transfer : sel->data.transfers.x11_to_wl) {
        if (transfer->handleSelectionNotify(event)) {
            return true;
        }
    }
    return false;
}

template<typename Selection>
bool handle_property_notify(Selection* sel, xcb_property_notify_event_t* event)
{
    for (auto& transfer : sel->data.transfers.x11_to_wl) {
        if (transfer->handlePropertyNotify(event)) {
            return true;
        }
    }
    for (auto& transfer : sel->data.transfers.wl_to_x11) {
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
        xcb_set_selection_owner(xcb_conn, sel->data.window, sel->data.atom, XCB_TIME_CURRENT_TIME);
    } else {
        sel->data.disown_pending = true;
        xcb_set_selection_owner(xcb_conn, XCB_WINDOW_NONE, sel->data.atom, sel->data.timestamp);
    }
    xcb_flush(xcb_conn);
}

template<typename Selection>
void overwrite_requestor_window(Selection* sel, xcb_window_t window)
{
    assert(sel->data.x11_source);
    sel->data.requestor_window = window == XCB_WINDOW_NONE ? sel->data.window : window;
}

// sets the current provider of the selection
template<typename Selection>
void set_wl_source(Selection* sel, WlSource<srv_data_device, srv_data_source>* source)
{
    delete sel->data.wayland_source;
    delete sel->data.x11_source;
    sel->data.wayland_source = nullptr;
    sel->data.x11_source = nullptr;
    if (source) {
        sel->data.wayland_source = source;
        QObject::connect(source->qobject(),
                         &qWlSource::transferReady,
                         sel->data.qobject.get(),
                         [sel](auto event, auto fd) { start_transfer_to_x11(sel, event, fd); });
    }
}

template<typename Selection>
void create_x11_source(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    delete sel->data.wayland_source;
    delete sel->data.x11_source;
    sel->data.wayland_source = nullptr;
    sel->data.x11_source = nullptr;
    if (!event || event->owner == XCB_WINDOW_NONE) {
        return;
    }
    sel->data.x11_source = new X11Source<clt_data_source>(event);

    QObject::connect(
        sel->data.x11_source->qobject(),
        &qX11Source::offersChanged,
        sel->data.qobject.get(),
        [sel](auto const& added, auto const& removed) { sel->x11OffersChanged(added, removed); });
    QObject::connect(sel->data.x11_source->qobject(),
                     &qX11Source::transferReady,
                     sel->data.qobject.get(),
                     [sel](auto target, auto fd) { start_transfer_to_wayland(sel, target, fd); });
}

template<typename Selection>
void start_transfer_to_wayland(Selection* sel, xcb_atom_t target, qint32 fd)
{
    // create new x to wl data transfer object
    auto transfer = new TransferXtoWl(sel->data.atom,
                                      target,
                                      fd,
                                      sel->data.x11_source->timestamp(),
                                      sel->data.requestor_window,
                                      sel->data.qobject.get());
    sel->data.transfers.x11_to_wl << transfer;

    QObject::connect(
        transfer, &TransferXtoWl::finished, sel->data.qobject.get(), [sel, transfer]() {
            Q_EMIT sel->data.qobject->transferFinished(transfer->timestamp());
            delete transfer;
            sel->data.transfers.x11_to_wl.removeOne(transfer);
            end_timeout_transfers_timer(sel);
        });
    start_timeout_transfers_timer(sel);
}

template<typename Selection>
void start_transfer_to_x11(Selection* sel, xcb_selection_request_event_t* event, qint32 fd)
{
    // create new wl to x data transfer object
    auto transfer = new TransferWltoX(sel->data.atom, event, fd, sel->data.qobject.get());

    QObject::connect(
        transfer, &TransferWltoX::selectionNotify, sel->data.qobject.get(), &sendSelectionNotify);
    QObject::connect(
        transfer, &TransferWltoX::finished, sel->data.qobject.get(), [sel, transfer]() {
            Q_EMIT sel->data.qobject->transferFinished(transfer->timestamp());

            // TODO: serialize? see comment below.
            //        const bool wasActive = (transfer == m_wlToXTransfers[0]);
            delete transfer;
            sel->data.transfers.wl_to_x11.removeOne(transfer);
            end_timeout_transfers_timer(sel);
            //        if (wasActive && !m_wlToXTransfers.isEmpty()) {
            //            m_wlToXTransfers[0]->startTransferFromSource();
            //        }
        });

    // add it to list of queued transfers
    sel->data.transfers.wl_to_x11.append(transfer);

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
    for (auto& transfer : sel->data.transfers.x11_to_wl) {
        transfer->timeout();
    }
    for (auto& transfer : sel->data.transfers.wl_to_x11) {
        transfer->timeout();
    }
}

template<typename Selection>
void start_timeout_transfers_timer(Selection* sel)
{
    if (sel->data.transfers.timeout) {
        return;
    }
    sel->data.transfers.timeout = new QTimer(sel->data.qobject.get());
    QObject::connect(sel->data.transfers.timeout,
                     &QTimer::timeout,
                     sel->data.qobject.get(),
                     [sel]() { timeout_transfers(sel); });
    sel->data.transfers.timeout->start(5000);
}

template<typename Selection>
void end_timeout_transfers_timer(Selection* sel)
{
    if (sel->data.transfers.x11_to_wl.isEmpty() && sel->data.transfers.wl_to_x11.isEmpty()) {
        delete sel->data.transfers.timeout;
        sel->data.transfers.timeout = nullptr;
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
