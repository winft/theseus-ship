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
#include "transfer.h"

#include "atoms.h"
#include "wayland_server.h"

#include <Wrapland/Server/data_source.h>

#include <string>
#include <unistd.h>

#include <xwayland_logging.h>

namespace KWin::Xwl
{

template<typename ServerSource>
WlSource<ServerSource>::WlSource(ServerSource* source)
    : server_source{source}
    , m_qobject(new qWlSource)
{
    assert(source);

    for (auto const& mime : source->mime_types()) {
        m_offers << QString::fromStdString(mime);
    }
    m_offerConnection
        = QObject::connect(source, &ServerSource::mime_type_offered, qobject(), [this](auto mime) {
              receiveOffer(mime);
          });
}

template<typename ServerSource>
WlSource<ServerSource>::~WlSource()
{
    delete m_qobject;
}

template<typename ServerSource>
void WlSource<ServerSource>::receiveOffer(std::string const& mime)
{
    m_offers << QString::fromStdString(mime);
}

template<typename ServerSource>
bool WlSource<ServerSource>::handleSelectionRequest(xcb_selection_request_event_t* event)
{
    if (event->target == atoms->targets) {
        sendTargets(event);
    } else if (event->target == atoms->timestamp) {
        sendTimestamp(event);
    } else if (event->target == atoms->delete_atom) {
        sendSelectionNotify(event, true);
    } else {
        // try to send mime data
        if (!checkStartTransfer(event)) {
            sendSelectionNotify(event, false);
        }
    }
    return true;
}

template<typename ServerSource>
void WlSource<ServerSource>::sendTargets(xcb_selection_request_event_t* event)
{
    QVector<xcb_atom_t> targets;
    targets.resize(m_offers.size() + 2);
    targets[0] = atoms->timestamp;
    targets[1] = atoms->targets;

    size_t cnt = 2;
    for (auto const& mime : m_offers) {
        targets[cnt] = mimeTypeToAtom(mime);
        cnt++;
    }

    xcb_change_property(kwinApp()->x11Connection(),
                        XCB_PROP_MODE_REPLACE,
                        event->requestor,
                        event->property,
                        XCB_ATOM_ATOM,
                        32,
                        cnt,
                        targets.data());
    sendSelectionNotify(event, true);
}

template<typename ServerSource>
void WlSource<ServerSource>::sendTimestamp(xcb_selection_request_event_t* event)
{
    auto const time = timestamp();
    xcb_change_property(kwinApp()->x11Connection(),
                        XCB_PROP_MODE_REPLACE,
                        event->requestor,
                        event->property,
                        XCB_ATOM_INTEGER,
                        32,
                        1,
                        &time);

    sendSelectionNotify(event, true);
}

template<typename ServerSource>
bool WlSource<ServerSource>::checkStartTransfer(xcb_selection_request_event_t* event)
{
    auto const targets = atomToMimeTypes(event->target);
    if (targets.isEmpty()) {
        qCDebug(KWIN_XWL) << "Unknown selection atom. Ignoring request.";
        return false;
    }
    std::string const firstTarget = targets[0].toUtf8().constData();

    auto cmp = [firstTarget](auto const& b) {
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

    Q_EMIT qobject()->transferReady(new xcb_selection_request_event_t(*event), p[0]);
    return true;
}

template<typename InternalSource>
X11Source<InternalSource>::X11Source(xcb_xfixes_selection_notify_event_t* event)
    : m_owner(event->owner)
    , m_timestamp(event->timestamp)
    , m_qobject(new qX11Source)
{
}

template<typename InternalSource>
X11Source<InternalSource>::~X11Source()
{
    delete m_qobject;
}

template<typename InternalSource>
void X11Source<InternalSource>::getTargets(xcb_window_t const window, xcb_atom_t const atom) const
{
    auto xcbConn = kwinApp()->x11Connection();
    /* will lead to a selection request event for the new owner */
    xcb_convert_selection(xcbConn, window, atom, atoms->targets, atoms->wl_selection, timestamp());
    xcb_flush(xcbConn);
}

using Mime = QPair<QString, xcb_atom_t>;

template<typename InternalSource>
void X11Source<InternalSource>::handleTargets(xcb_window_t const requestor)
{
    // receive targets
    auto xcbConn = kwinApp()->x11Connection();
    xcb_get_property_cookie_t cookie = xcb_get_property(
        xcbConn, 1, requestor, atoms->wl_selection, XCB_GET_PROPERTY_TYPE_ANY, 0, 4096);
    auto reply = xcb_get_property_reply(xcbConn, cookie, nullptr);
    if (!reply) {
        return;
    }
    if (reply->type != XCB_ATOM_ATOM) {
        free(reply);
        return;
    }

    QStringList added;
    QStringList removed;

    Mimes all;
    auto value = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
    for (uint32_t i = 0; i < reply->value_len; i++) {
        if (value[i] == XCB_ATOM_NONE) {
            continue;
        }

        auto const mimeStrings = atomToMimeTypes(value[i]);
        if (mimeStrings.isEmpty()) {
            // TODO: this should never happen? assert?
            continue;
        }

        auto const mimeIt
            = std::find_if(m_offers.begin(), m_offers.end(), [value, i](auto const& mime) {
                  return mime.second == value[i];
              });

        auto mimePair = Mime(mimeStrings[0], value[i]);
        if (mimeIt == m_offers.end()) {
            added << mimePair.first;
        } else {
            m_offers.removeAll(mimePair);
        }
        all << mimePair;
    }
    // all left in m_offers are not in the updated targets
    for (auto const& mimePair : m_offers) {
        removed << mimePair.first;
    }
    m_offers = all;

    if (!added.isEmpty() || !removed.isEmpty()) {
        Q_EMIT qobject()->offersChanged(added, removed);
    }

    free(reply);
}

template<typename InternalSource>
void X11Source<InternalSource>::setSource(InternalSource* src)
{
    Q_ASSERT(src);
    if (m_source) {
        delete m_source;
    }

    m_source = src;

    for (auto const& offer : m_offers) {
        src->offer(offer.first.toStdString());
    }

    QObject::connect(
        src, &InternalSource::data_requested, qobject(), [this](auto const& mimeName, auto fd) {
            startTransfer(QString::fromStdString(mimeName), fd);
        });
}

template<typename InternalSource>
void X11Source<InternalSource>::setOffers(Mimes const& offers)
{
    // TODO: share code with handleTargets and emit signals accordingly?
    m_offers = offers;
}

template<typename InternalSource>
bool X11Source<InternalSource>::handleSelectionNotify(xcb_selection_notify_event_t* event)
{
    if (event->property == XCB_ATOM_NONE) {
        qCWarning(KWIN_XWL) << "Incoming X selection conversion failed";
        return true;
    }
    if (event->target == atoms->targets) {
        handleTargets(event->requestor);
        return true;
    }
    return false;
}

template<typename InternalSource>
void X11Source<InternalSource>::startTransfer(QString const& mimeName, qint32 fd)
{
    auto const mimeIt
        = std::find_if(m_offers.begin(), m_offers.end(), [mimeName](auto const& mime) {
              return mime.first == mimeName;
          });
    if (mimeIt == m_offers.end()) {
        qCDebug(KWIN_XWL) << "Sending X11 clipboard to Wayland failed: unsupported MIME.";
        close(fd);
        return;
    }

    Q_EMIT qobject()->transferReady((*mimeIt).second, fd);
}

// Templates specializations
template class WlSource<Wrapland::Server::data_source>;
template class X11Source<data_source_ext>;
template class WlSource<Wrapland::Server::primary_selection_source>;
template class X11Source<primary_selection_source_ext>;
}
