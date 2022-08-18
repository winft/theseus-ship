/*
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection_wl.h"
#include "selection_x11.h"
#include "transfer.h"

#include <QObject>
#include <xcb/xcb_event.h>
#include <xcb/xfixes.h>

namespace KWin::xwl
{

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
        sel->data.wayland_source->timestamp = event->timestamp;
        sel->data.timestamp = event->timestamp;
        return true;
    }

    // Being here means some other X window has claimed the selection.

    // TODO(romangg): Use C++20 require on the member function and otherwise call the free function.
    sel->do_handle_xfixes_notify(event);
    return true;
}

template<typename Selection>
void do_handle_xfixes_notify(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    // In case we had an X11 source, we need to delete it directly if there is no new one.
    // But if there is a new one don't delete it, as this might trigger data-control clients.
    auto had_x11_source = static_cast<bool>(sel->data.x11_source);

    sel->data.x11_source.reset();

    auto const& client = sel->data.core.space->active_client;
    if (!dynamic_cast<typename Selection::window_t::space_t::x11_window const*>(client)) {
        // Clipboard is only allowed to be acquired when Xwayland has focus
        // TODO(romangg): can we make this stronger (window id comparison)?
        if (had_x11_source) {
            sel->data.source_int.reset();
        }
        return;
    }

    create_x11_source(sel, event);

    if (auto const& source = sel->data.x11_source) {
        /* Gets X11 targets, will lead to a selection request event for the new owner. */
        xcb_convert_selection(source->core.x11.connection,
                              sel->data.requestor_window,
                              sel->data.atom,
                              sel->data.core.space->atoms->targets,
                              sel->data.core.space->atoms->wl_selection,
                              source->timestamp);
        xcb_flush(source->core.x11.connection);
    }
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
        // TODO(romangg): Use C++20 require on the member function.
        return sel->handle_client_message(reinterpret_cast<xcb_client_message_event_t*>(event));
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

    if (!dynamic_cast<typename Selection::window_t::space_t::x11_window*>(
            sel->data.core.space->active_client)) {
        // Receiving Wayland selection not allowed when no Xwayland surface active
        // filter the event, but don't act upon it
        send_selection_notify(sel->data.core.x11.connection, event, false);
        return true;
    }

    if (sel->data.window != event->owner || !sel->data.wayland_source) {
        if (event->time < sel->data.timestamp) {
            // cancel earlier attempts at receiving a selection
            // TODO: is this for sure without problems?
            send_selection_notify(sel->data.core.x11.connection, event, false);
            return true;
        }
        return false;
    }

    return selection_wl_handle_request(sel->data.wayland_source, event);
}

template<typename Selection>
bool handle_selection_notify(Selection* sel, xcb_selection_notify_event_t* event)
{
    if (sel->data.x11_source && event->requestor == sel->data.requestor_window
        && event->selection == sel->data.atom) {
        if (selection_x11_handle_notify(sel->data.x11_source, event)) {
            return true;
        }
    }

    for (auto& transfer : sel->data.transfers.x11_to_wl) {
        if (transfer->handle_selection_notify(event)) {
            return true;
        }
    }

    return false;
}

template<typename Selection>
bool handle_property_notify(Selection* sel, xcb_property_notify_event_t* event)
{
    for (auto& transfer : sel->data.transfers.x11_to_wl) {
        if (transfer->handle_property_notify(event)) {
            return true;
        }
    }

    for (auto& transfer : sel->data.transfers.wl_to_x11) {
        if (transfer->handle_property_notify(event)) {
            return true;
        }
    }

    return false;
}

}
