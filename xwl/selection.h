/*
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection_source.h"
#include "sources.h"
#include "transfer.h"
#include "xwayland.h"

#include "atoms.h"
#include "wayland_server.h"
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

namespace KWin::Xwl
{
class TransferWltoX;
class TransferXtoWl;
template<typename>
class WlSource;
template<typename>
class X11Source;

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
 * Data needed by X selections and their Wayland counter-parts.
 *
 * A selection should exist through the whole runtime of an Xwayland
 * session.
 * Each selection holds an independent instance of this class,
 * containing the source and the active transfers.
 *
 * This class can be specialized to support the core Wayland protocol
 * (clipboard and dnd) as well as primary selection.
 */
template<typename server_source, typename int_source>
struct selection_data {
    using internal_source = int_source;

    std::unique_ptr<q_selection> qobject;

    xcb_atom_t atom{XCB_ATOM_NONE};
    xcb_window_t window{XCB_WINDOW_NONE};

    bool disown_pending{false};
    xcb_timestamp_t timestamp;
    xcb_window_t requestor_window{XCB_WINDOW_NONE};

    // Active source, if any. Only one of them at max can exist
    // at the same time.
    WlSource<server_source>* wayland_source{nullptr};
    X11Source<internal_source>* x11_source{nullptr};

    internal_source* source_int{nullptr};

    x11_data x11;

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
        delete source_int;
        delete wayland_source;
        delete x11_source;
        source_int = nullptr;
        wayland_source = nullptr;
        x11_source = nullptr;
    }
};

template<typename server_source, typename internal_source>
auto create_selection_data(xcb_atom_t atom, x11_data const& x11)
{
    selection_data<server_source, internal_source> sel;

    sel.qobject.reset(new q_selection());
    sel.atom = atom;
    sel.x11 = x11;

    sel.window = xcb_generate_id(x11.connection);
    sel.requestor_window = sel.window;
    xcb_flush(x11.connection);

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

    auto xcbConn = kwinApp()->x11Connection();
    xcb_send_event(xcbConn, 0, event->requestor, XCB_EVENT_MASK_NO_EVENT, (char const*)&notify);
    xcb_flush(xcbConn);
}

template<typename Selection>
void register_xfixes(Selection* sel)
{
    auto xcb_conn = kwinApp()->x11Connection();
    uint32_t const mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER
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
    do_handle_xfixes_notify(sel, event);
    return true;
}

template<typename Selection>
void do_handle_xfixes_notify(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    // In case we had an X11 source, we need to delete it directly if there is no new one.
    // But if there is a new one don't delete it, as this might trigger data-control clients.
    auto source_int = sel->data.x11_source ? sel->data.source_int : nullptr;

    create_x11_source(sel, nullptr);

    auto const& client = workspace()->activeClient();
    if (!qobject_cast<win::x11::window const*>(client)) {
        // clipboard is only allowed to be acquired when Xwayland has focus
        // TODO: can we make this stronger (window id comparison)?
        if (source_int) {
            delete source_int;
            sel->data.source_int = nullptr;
        }
        return;
    }

    create_x11_source(sel, event);

    if (auto const& source = sel->data.x11_source) {
        source->getTargets(sel->data.requestor_window, sel->data.atom);
    }
}

template<typename Selection>
bool handle_client_message([[maybe_unused]] Selection* sel,
                           [[maybe_unused]] xcb_client_message_event_t* event)
{
    return false;
}

template<typename Selection>
bool filter_event(Selection* sel, xcb_generic_event_t* event)
{
    if (!sel) {
        // A selection event might be received before the client connection for our selection has
        // been established.
        // TODO(romangg): Can we ensure that is done before we receive any event?
        return false;
    }

    switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
    case XCB_SELECTION_NOTIFY:
        return handle_selection_notify(sel, reinterpret_cast<xcb_selection_notify_event_t*>(event));
    case XCB_PROPERTY_NOTIFY:
        return handle_property_notify(sel, reinterpret_cast<xcb_property_notify_event_t*>(event));
    case XCB_SELECTION_REQUEST:
        return handle_selection_request(sel,
                                        reinterpret_cast<xcb_selection_request_event_t*>(event));
    case XCB_CLIENT_MESSAGE:
        return handle_client_message(sel, reinterpret_cast<xcb_client_message_event_t*>(event));
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
    auto xcb_conn = sel->data.x11.connection;
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
template<typename Selection, typename server_source>
void set_wl_source(Selection* sel, WlSource<server_source>* source)
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
    delete sel->data.x11_source;
    sel->data.x11_source = nullptr;

    if (!event || event->owner == XCB_WINDOW_NONE) {
        return;
    }

    delete sel->data.wayland_source;
    sel->data.wayland_source = nullptr;

    using internal_source = typename decltype(sel->data)::internal_source;
    sel->data.x11_source = new X11Source<internal_source>(event);

    QObject::connect(sel->data.x11_source->qobject(),
                     &qX11Source::offersChanged,
                     sel->data.qobject.get(),
                     [sel](auto const& added, auto const& removed) {
                         handle_x11_offer_change(sel, added, removed);
                     });
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
                                      sel->data.x11,
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

inline xcb_atom_t mimeTypeToAtomLiteral(QString const& mimeType)
{
    return Xcb::Atom(mimeType.toLatin1(), false, kwinApp()->x11Connection());
}

inline xcb_atom_t mimeTypeToAtom(QString const& mimeType)
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
    auto xcbConn = kwinApp()->x11Connection();
    auto nameCookie = xcb_get_atom_name(xcbConn, atom);
    auto nameReply = xcb_get_atom_name_reply(xcbConn, nameCookie, nullptr);
    if (!nameReply) {
        return QString();
    }

    auto const length = xcb_get_atom_name_name_length(nameReply);
    auto const name = QString::fromLatin1(xcb_get_atom_name_name(nameReply), length);
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

template<typename Selection>
void register_x11_selection(Selection* sel, QSize const& window_size)
{
    auto xcbConn = sel->data.x11.connection;

    uint32_t const values[] = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_create_window(xcbConn,
                      XCB_COPY_FROM_PARENT,
                      sel->data.window,
                      kwinApp()->x11RootWindow(),
                      0,
                      0,
                      window_size.width(),
                      window_size.height(),
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      sel->data.x11.screen->root_visual,
                      XCB_CW_EVENT_MASK,
                      values);
    register_xfixes(sel);
    xcb_flush(xcbConn);
}

template<typename Selection>
void cleanup_wl_to_x11_source(Selection* sel)
{
    using server_source = typename Selection::server_source;

    set_wl_source<Selection, server_source>(sel, nullptr);
    own_selection(sel, false);
}

template<typename Selection>
void handle_wl_selection_client_change(Selection* sel)
{
    using server_source = typename Selection::server_source;

    auto srv_src = sel->get_current_source();
    static_assert(std::is_same_v<server_source, std::remove_pointer_t<decltype(srv_src)>>,
                  "get current data source type mismatch");

    if (!qobject_cast<win::x11::window*>(workspace()->activeClient())) {
        // No active client or active client is Wayland native.
        if (sel->data.wayland_source) {
            cleanup_wl_to_x11_source(sel);
        }
        return;
    }

    // Xwayland client is active and we need a Wayland source
    if (sel->data.wayland_source) {
        // source already exists, nothing more to do
        return;
    }

    auto wls = new WlSource<server_source>(srv_src);
    set_wl_source(sel, wls);
    own_selection(sel, true);
}

/**
 * React to Wl selection change.
 */
template<typename Selection>
void handle_wl_selection_change(Selection* sel)
{
    using server_source = typename Selection::server_source;

    auto srv_src = sel->get_current_source();
    static_assert(std::is_same_v<server_source, std::remove_pointer_t<decltype(srv_src)>>,
                  "get current data source type mismatch");

    auto cleanup_activation_notifier = [&] {
        QObject::disconnect(sel->source_check_connection);
        sel->source_check_connection = QMetaObject::Connection();
    };

    // Wayland source gets created when:
    // - the Wl selection exists,
    // - its source is not Xwayland,
    // - a client is active,
    // - this client is an Xwayland one.
    //
    // In all other cases the Wayland source gets destroyed to shield against snooping X clients.

    if (!srv_src) {
        // Wayland selection has been removed.
        cleanup_activation_notifier();
        cleanup_wl_to_x11_source(sel);
        return;
    }

    if (sel->data.source_int && sel->data.source_int->src() == srv_src) {
        // Wayland selection has been changed to our internal Xwayland source. Nothing to do.
        cleanup_activation_notifier();
        return;
    }

    // Wayland native client provides new selection.
    if (!sel->source_check_connection) {
        sel->source_check_connection = QObject::connect(
            workspace(), &Workspace::clientActivated, sel->data.qobject.get(), [sel] {
                handle_wl_selection_client_change(sel);
            });
    }

    delete sel->data.wayland_source;
    sel->data.wayland_source = nullptr;

    handle_wl_selection_client_change(sel);
}

template<typename Selection>
void handle_x11_offer_change(Selection* sel, QStringList const& added, QStringList const& removed)
{
    using internal_source = typename Selection::internal_source;

    auto source = sel->data.x11_source;

    if (!source) {
        return;
    }

    auto const offers = source->offers();
    if (offers.isEmpty()) {
        sel->get_selection_setter()(nullptr);
        return;
    }

    if (!source->source() || !removed.isEmpty()) {
        // create new Wl DataSource if there is none or when types
        // were removed (Wl Data Sources can only add types)
        auto old_source_int = sel->data.source_int;
        auto internal_src = new internal_source();

        sel->data.source_int = internal_src;
        source->setSource(internal_src);
        sel->get_selection_setter()(internal_src->src());

        // Delete old internal source after setting the new one so data-control devices won't
        // receive an intermediate null selection and send it back to us overriding our new one.
        delete old_source_int;
    } else if (auto dataSource = source->source()) {
        for (auto const& mime : added) {
            dataSource->offer(mime.toStdString());
        }
    }
}

}
