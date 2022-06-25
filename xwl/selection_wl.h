/*
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "mime.h"
#include "selection_data.h"
#include "sources.h"
#include "transfer.h"
#include "types.h"

#include "win/space.h"
#include "win/x11/window.h"

#include <QObject>
#include <unistd.h>
#include <xwayland_logging.h>

namespace KWin::xwl
{

inline void send_selection_notify(xcb_connection_t* connection,
                                  xcb_selection_request_event_t* event,
                                  bool success)
{
    xcb_selection_notify_event_t notify;
    notify.response_type = XCB_SELECTION_NOTIFY;
    notify.sequence = 0;
    notify.time = event->time;
    notify.requestor = event->requestor;
    notify.selection = event->selection;
    notify.target = event->target;
    notify.property = success ? event->property : xcb_atom_t(XCB_ATOM_NONE);

    xcb_send_event(connection,
                   0,
                   event->requestor,
                   XCB_EVENT_MASK_NO_EVENT,
                   reinterpret_cast<char const*>(&notify));
    xcb_flush(connection);
}

// must be called in order to provide data from Wl to X
template<typename Selection>
void own_selection(Selection* sel, bool own)
{
    auto xcb_con = sel->data.x11.connection;

    if (own) {
        xcb_set_selection_owner(xcb_con, sel->data.window, sel->data.atom, XCB_TIME_CURRENT_TIME);
    } else {
        sel->data.disown_pending = true;
        xcb_set_selection_owner(xcb_con, XCB_WINDOW_NONE, sel->data.atom, sel->data.timestamp);
    }

    xcb_flush(xcb_con);
}

// sets the current provider of the selection
template<typename Selection, typename server_source>
void set_wl_source(Selection* sel, wl_source<server_source>* source)
{
    sel->data.wayland_source.reset();
    sel->data.x11_source.reset();

    if (source) {
        sel->data.wayland_source.reset(source);
        QObject::connect(source->get_qobject(),
                         &q_wl_source::transfer_ready,
                         sel->data.qobject.get(),
                         [sel](auto event, auto fd) { start_transfer_to_x11(sel, event, fd); });
    }
}

template<typename Selection>
void start_transfer_to_x11(Selection* sel, xcb_selection_request_event_t* event, qint32 fd)
{
    auto transfer = new wl_to_x11_transfer(
        sel->data.atom, event, fd, *sel->data.x11.space->atoms, sel->data.qobject.get());

    QObject::connect(transfer,
                     &wl_to_x11_transfer::selection_notify,
                     sel->data.qobject.get(),
                     [con = sel->data.x11.connection](auto event, auto success) {
                         send_selection_notify(con, event, success);
                     });
    QObject::connect(
        transfer, &wl_to_x11_transfer::finished, sel->data.qobject.get(), [sel, transfer]() {
            Q_EMIT sel->data.qobject->transfer_finished(transfer->get_timestamp());

            // TODO(romangg): Serialize? see comment below.
            delete transfer;
            remove_all(sel->data.transfers.wl_to_x11, transfer);
            end_timeout_transfers_timer(sel);
        });

    // Add it to list of queued transfers.
    sel->data.transfers.wl_to_x11.push_back(transfer);

    // TODO(romangg): Do we need to serialize the transfers, or can we do
    //                them in parallel as we do it right now?
    transfer->start_transfer_from_source();
    start_timeout_transfers_timer(sel);
}

template<typename Selection>
void cleanup_wl_to_x11_source(Selection* sel)
{
    using server_source = std::remove_pointer_t<decltype(sel->get_current_source())>;

    set_wl_source<Selection, server_source>(sel, nullptr);
    own_selection(sel, false);
}

template<typename Selection>
void handle_wl_selection_client_change(Selection* sel)
{
    auto srv_src = sel->get_current_source();

    if (!qobject_cast<win::x11::window*>(sel->data.x11.space->active_client)) {
        // No active client or active client is Wayland native.
        if (sel->data.wayland_source) {
            cleanup_wl_to_x11_source(sel);
        }
        return;
    }

    // At this point we know an Xwayland client is active and that we need a Wayland source.

    if (sel->data.wayland_source) {
        // Source already exists, we can reuse it.
        return;
    }

    using server_source = std::remove_pointer_t<decltype(srv_src)>;
    auto wls = new wl_source<server_source>(srv_src, sel->data.x11);

    set_wl_source(sel, wls);
    own_selection(sel, true);
}

/**
 * React to Wl selection change.
 */
template<typename Selection>
void handle_wl_selection_change(Selection* sel)
{
    auto srv_src = sel->get_current_source();

    auto cleanup_activation_notifier = [&] {
        QObject::disconnect(sel->data.active_window_notifier);
        sel->data.active_window_notifier = QMetaObject::Connection();
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
    if (!sel->data.active_window_notifier) {
        sel->data.active_window_notifier
            = QObject::connect(sel->data.x11.space->qobject.get(),
                               &win::space::qobject_t::clientActivated,
                               sel->data.qobject.get(),
                               [sel] { handle_wl_selection_client_change(sel); });
    }

    sel->data.wayland_source.reset();

    handle_wl_selection_client_change(sel);
}

inline void send_wl_selection_timestamp(x11_data const& x11,
                                        xcb_selection_request_event_t* event,
                                        xcb_timestamp_t time)
{
    xcb_change_property(x11.connection,
                        XCB_PROP_MODE_REPLACE,
                        event->requestor,
                        event->property,
                        XCB_ATOM_INTEGER,
                        32,
                        1,
                        &time);

    send_selection_notify(x11.connection, event, true);
}

inline void send_wl_selection_targets(x11_data const& x11,
                                      xcb_selection_request_event_t* event,
                                      std::vector<std::string> const& offers)
{
    std::vector<xcb_atom_t> targets;
    targets.resize(offers.size() + 2);
    targets[0] = x11.space->atoms->timestamp;
    targets[1] = x11.space->atoms->targets;

    size_t cnt = 2;
    for (auto const& mime : offers) {
        targets[cnt] = mime_type_to_atom(mime, *x11.space->atoms);
        cnt++;
    }

    xcb_change_property(x11.connection,
                        XCB_PROP_MODE_REPLACE,
                        event->requestor,
                        event->property,
                        XCB_ATOM_ATOM,
                        32,
                        cnt,
                        targets.data());

    send_selection_notify(x11.connection, event, true);
}

/// Returns the file descriptor to write in or -1 on error.
template<typename Source>
int selection_wl_start_transfer(Source&& source, xcb_selection_request_event_t* event)
{
    auto const targets = atom_to_mime_types(event->target, *source->x11.space->atoms);
    if (targets.empty()) {
        qCDebug(KWIN_XWL) << "Unknown selection atom. Ignoring request.";
        return -1;
    }

    auto const firstTarget = targets[0];

    auto cmp = [&firstTarget](auto const& b) {
        if (firstTarget == "text/uri-list") {
            // Wayland sources might announce the old mime or the new standard
            return firstTarget == b || b == "text/x-uri";
        }
        return firstTarget == b;
    };

    // check supported mimes
    auto const offers = source->server_source->mime_types();
    auto const mimeIt = std::find_if(offers.begin(), offers.end(), cmp);
    if (mimeIt == offers.end()) {
        // Requested Mime not supported. Not sending selection.
        return -1;
    }

    int p[2];
    if (pipe(p) == -1) {
        qCWarning(KWIN_XWL) << "Pipe failed. Not sending selection.";
        return -1;
    }

    source->server_source->request_data(*mimeIt, p[1]);
    return p[0];
}

template<typename Source>
bool selection_wl_handle_request(Source&& source, xcb_selection_request_event_t* event)
{
    auto& x11 = source->x11;

    if (event->target == x11.space->atoms->targets) {
        send_wl_selection_targets(x11, event, source->offers);
    } else if (event->target == x11.space->atoms->timestamp) {
        send_wl_selection_timestamp(x11, event, source->timestamp);
    } else if (event->target == x11.space->atoms->delete_atom) {
        send_selection_notify(x11.connection, event, true);
    } else {
        // try to send mime data
        if (auto fd = selection_wl_start_transfer(source, event); fd > 0) {
            Q_EMIT source->get_qobject()->transfer_ready(new xcb_selection_request_event_t(*event),
                                                         fd);
        } else {
            send_selection_notify(x11.connection, event, false);
        }
    }
    return true;
}

}
