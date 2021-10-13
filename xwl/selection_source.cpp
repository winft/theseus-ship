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
#include "selection_source.h"

#include "selection.h"
#include "types.h"

#include "wayland_server.h"

#include <Wrapland/Server/data_source.h>

#include <string>
#include <unistd.h>

#include <xwayland_logging.h>

namespace KWin::xwl
{

template<typename ServerSource>
wl_source<ServerSource>::wl_source(ServerSource* source, xcb_connection_t* connection)
    : server_source{source}
    , connection{connection}
    , qobject{new q_wl_source}
{
    assert(source);

    for (auto const& mime : source->mime_types()) {
        offers.emplace_back(mime);
    }

    QObject::connect(source, &ServerSource::mime_type_offered, get_qobject(), [this](auto mime) {
        receive_offer(mime);
    });
}

template<typename ServerSource>
wl_source<ServerSource>::~wl_source()
{
    delete qobject;
}

template<typename ServerSource>
void wl_source<ServerSource>::receive_offer(std::string const& mime)
{
    offers.emplace_back(mime);
}

template<typename ServerSource>
bool wl_source<ServerSource>::handle_selection_request(xcb_selection_request_event_t* event)
{
    if (event->target == atoms->targets) {
        send_targets(event);
    } else if (event->target == atoms->timestamp) {
        send_timestamp(event);
    } else if (event->target == atoms->delete_atom) {
        send_selection_notify(event, true);
    } else {
        // try to send mime data
        if (!check_start_transfer(event)) {
            send_selection_notify(event, false);
        }
    }
    return true;
}

template<typename ServerSource>
void wl_source<ServerSource>::send_targets(xcb_selection_request_event_t* event)
{
    std::vector<xcb_atom_t> targets;
    targets.resize(offers.size() + 2);
    targets[0] = atoms->timestamp;
    targets[1] = atoms->targets;

    size_t cnt = 2;
    for (auto const& mime : offers) {
        targets[cnt] = mime_type_to_atom(mime);
        cnt++;
    }

    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        event->requestor,
                        event->property,
                        XCB_ATOM_ATOM,
                        32,
                        cnt,
                        targets.data());

    send_selection_notify(event, true);
}

template<typename ServerSource>
void wl_source<ServerSource>::send_timestamp(xcb_selection_request_event_t* event)
{
    auto const time = get_timestamp();
    xcb_change_property(connection,
                        XCB_PROP_MODE_REPLACE,
                        event->requestor,
                        event->property,
                        XCB_ATOM_INTEGER,
                        32,
                        1,
                        &time);

    send_selection_notify(event, true);
}

template<typename ServerSource>
bool wl_source<ServerSource>::check_start_transfer(xcb_selection_request_event_t* event)
{
    auto const targets = atom_to_mime_types(event->target);
    if (targets.empty()) {
        qCDebug(KWIN_XWL) << "Unknown selection atom. Ignoring request.";
        return false;
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
    auto const offers = server_source->mime_types();
    auto const mimeIt = std::find_if(offers.begin(), offers.end(), cmp);
    if (mimeIt == offers.end()) {
        // Requested Mime not supported. Not sending selection.
        return false;
    }

    int p[2];
    if (pipe(p) == -1) {
        qCWarning(KWIN_XWL) << "Pipe failed. Not sending selection.";
        return false;
    }

    server_source->request_data(*mimeIt, p[1]);
    waylandServer()->dispatch();

    Q_EMIT get_qobject()->transfer_ready(new xcb_selection_request_event_t(*event), p[0]);
    return true;
}

template<typename InternalSource>
x11_source<InternalSource>::x11_source(xcb_xfixes_selection_notify_event_t* event,
                                       x11_data const& x11)
    : x11{x11}
    , timestamp{event->timestamp}
    , qobject{new q_x11_source}
{
}

template<typename InternalSource>
x11_source<InternalSource>::~x11_source()
{
    delete qobject;
}

template<typename InternalSource>
void x11_source<InternalSource>::get_targets(xcb_window_t const window, xcb_atom_t const atom) const
{
    /* will lead to a selection request event for the new owner */
    xcb_convert_selection(
        x11.connection, window, atom, atoms->targets, atoms->wl_selection, timestamp);
    xcb_flush(x11.connection);
}

template<typename InternalSource>
void x11_source<InternalSource>::handle_targets(xcb_window_t const requestor)
{
    // receive targets
    xcb_get_property_cookie_t cookie = xcb_get_property(
        x11.connection, 1, requestor, atoms->wl_selection, XCB_GET_PROPERTY_TYPE_ANY, 0, 4096);
    auto reply = xcb_get_property_reply(x11.connection, cookie, nullptr);
    if (!reply) {
        return;
    }
    if (reply->type != XCB_ATOM_ATOM) {
        free(reply);
        return;
    }

    std::vector<std::string> added;
    std::vector<std::string> removed;

    mime_atoms all;

    auto value = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
    for (uint32_t i = 0; i < reply->value_len; i++) {
        if (value[i] == XCB_ATOM_NONE) {
            continue;
        }

        auto const mimeStrings = atom_to_mime_types(value[i]);
        if (mimeStrings.empty()) {
            // TODO: this should never happen? assert?
            continue;
        }

        auto const mimeIt
            = std::find_if(offers.begin(), offers.end(), [value, i](auto const& mime) {
                  return mime.atom == value[i];
              });

        auto mimePair = mime_atom{mimeStrings[0], value[i]};
        if (mimeIt == offers.end()) {
            added.emplace_back(mimePair.id);
        } else {
            remove_all(offers, mimePair);
        }
        all.emplace_back(mimePair);
    }
    // all left in offers are not in the updated targets
    for (auto const& mimePair : offers) {
        removed.emplace_back(mimePair.id);
    }
    offers = all;

    if (!added.empty() || !removed.empty()) {
        Q_EMIT qobject->offers_changed(added, removed);
    }

    free(reply);
}

template<typename InternalSource>
void x11_source<InternalSource>::set_source(InternalSource* src)
{
    Q_ASSERT(src);
    if (source) {
        delete source;
    }

    source = src;

    for (auto const& offer : offers) {
        src->offer(offer.id);
    }

    QObject::connect(src,
                     &InternalSource::data_requested,
                     get_qobject(),
                     [this](auto const& mimeName, auto fd) { start_transfer(mimeName, fd); });
}

template<typename InternalSource>
void x11_source<InternalSource>::set_offers(mime_atoms const& offers)
{
    // TODO: share code with handle_targets and emit signals accordingly?
    this->offers = offers;
}

template<typename InternalSource>
bool x11_source<InternalSource>::handle_selection_notify(xcb_selection_notify_event_t* event)
{
    if (event->property == XCB_ATOM_NONE) {
        qCWarning(KWIN_XWL) << "Incoming X selection conversion failed";
        return true;
    }
    if (event->target == atoms->targets) {
        handle_targets(event->requestor);
        return true;
    }
    return false;
}

template<typename InternalSource>
void x11_source<InternalSource>::start_transfer(std::string const& mimeName, qint32 fd)
{
    auto const mimeIt = std::find_if(offers.begin(), offers.end(), [&mimeName](auto const& mime) {
        return mime.id == mimeName;
    });
    if (mimeIt == offers.end()) {
        qCDebug(KWIN_XWL) << "Sending X11 clipboard to Wayland failed: unsupported MIME.";
        close(fd);
        return;
    }

    Q_EMIT get_qobject()->transfer_ready(mimeIt->atom, fd);
}

// Templates specializations
template class wl_source<Wrapland::Server::data_source>;
template class x11_source<data_source_ext>;
template class wl_source<Wrapland::Server::primary_selection_source>;
template class x11_source<primary_selection_source_ext>;
}
