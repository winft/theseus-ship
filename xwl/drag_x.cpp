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
#include "drag_x.h"

#include "dnd.h"
#include "selection.h"
#include "selection_source.h"
#include "xwayland.h"

#include "atoms.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "win/stacking_order.h"
#include "workspace.h"

#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <QMouseEvent>
#include <QTimer>

namespace KWin::Xwl
{

XToWlDrag::XToWlDrag(DataX11Source* source, Dnd* dnd)
    : Drag(dnd)
    , m_source(source)
{
    connect(dnd->data.qobject.get(),
            &q_selection::transferFinished,
            this,
            [this](xcb_timestamp_t eventTime) {
                // we use this mechanism, because the finished call is not
                // reliable done by Wayland clients
                auto it = std::find_if(
                    m_dataRequests.begin(), m_dataRequests.end(), [eventTime](auto const& req) {
                        return req.first == eventTime && req.second == false;
                    });
                if (it == m_dataRequests.end()) {
                    // transfer finished for a different drag
                    return;
                }
                (*it).second = true;
                checkForFinished();
            });
    connect(
        source->qobject(), &qX11Source::transferReady, this, [this](xcb_atom_t target, qint32 fd) {
            Q_UNUSED(target);
            Q_UNUSED(fd);
            m_dataRequests << QPair<xcb_timestamp_t, bool>(m_source->timestamp(), false);
        });

    data_source.reset(new data_source_ext);
    auto source_int_ptr = data_source.get();

    assert(!dnd->data.source_int);
    dnd->data.source_int = source_int_ptr;

    connect(source_int_ptr, &data_source_ext::accepted, this, [this](auto /*mime_type*/) {
        // TODO(romangg): handle?
    });
    connect(source_int_ptr, &data_source_ext::dropped, this, [this] {
        m_performed = true;
        if (m_visit) {
            connect(m_visit.get(), &WlVisit::finish, this, [this](WlVisit* visit) {
                Q_UNUSED(visit);
                checkForFinished();
            });

            QTimer::singleShot(2000, this, [this] {
                if (!m_visit->entered() || !m_visit->dropHandled()) {
                    // X client timed out
                    Q_EMIT finish(this);
                } else if (m_dataRequests.size() == 0) {
                    // Wl client timed out
                    m_visit->sendFinished();
                    Q_EMIT finish(this);
                }
            });
        }
        checkForFinished();
    });
    connect(source_int_ptr, &data_source_ext::finished, this, [this] {
        // this call is not reliably initiated by Wayland clients
        checkForFinished();
    });
    connect(source_int_ptr, &data_source_ext::action, this, [this](auto action) {
        m_lastSelectedDragAndDropAction = action;
    });

    // source does _not_ take ownership of source_int_ptr
    source->setSource(source_int_ptr);
}

XToWlDrag::~XToWlDrag()
{
    delete dnd->data.source_int;
    dnd->data.source_int = nullptr;
}

DragEventReply XToWlDrag::moveFilter(Toplevel* target, QPoint const& pos)
{
    Q_UNUSED(pos);

    auto seat = waylandServer()->seat();

    if (m_visit && m_visit->target() == target) {
        // still same Wl target, wait for X events
        return DragEventReply::Ignore;
    }

    auto const had_visit = static_cast<bool>(m_visit);
    if (m_visit) {
        if (m_visit->leave()) {
            m_visit.reset();
        } else {
            connect(m_visit.get(), &WlVisit::finish, this, [this](WlVisit* visit) {
                remove_all_if(m_oldVisits, [visit](auto&& old) { return old.get() == visit; });
            });
            m_oldVisits.emplace_back(m_visit.release());
        }
    }

    if (!target || !target->surface()
        || target->surface()->client() == waylandServer()->xWaylandConnection()) {
        // Currently there is no target or target is an Xwayland window.
        // Handled here and by X directly.
        if (target && target->surface() && target->control) {
            if (workspace()->activeClient() != target) {
                workspace()->activateClient(target);
            }
        }

        if (had_visit) {
            // Last received enter event is now void. Wait for the next one.
            seat->drags().set_target(nullptr);
        }
        return DragEventReply::Ignore;
    }

    // New Wl native target.
    m_visit.reset(new WlVisit(target, this));
    connect(m_visit.get(), &WlVisit::offersReceived, this, &XToWlDrag::setOffers);
    return DragEventReply::Ignore;
}

bool XToWlDrag::handleClientMessage(xcb_client_message_event_t* event)
{
    for (auto const& visit : m_oldVisits) {
        if (visit->handleClientMessage(event)) {
            return true;
        }
    }

    if (m_visit && m_visit->handleClientMessage(event)) {
        return true;
    }

    return false;
}

void XToWlDrag::setDragAndDropAction(DnDAction action)
{
    data_source->set_actions(action);
}

DnDAction XToWlDrag::selectedDragAndDropAction()
{
    return m_lastSelectedDragAndDropAction;
}

void XToWlDrag::setOffers(Mimes const& offers)
{
    m_source->setOffers(offers);

    if (offers.isEmpty()) {
        // There are no offers, so just directly set the drag target,
        // no transfer possible anyways.
        setDragTarget();
        return;
    }

    if (m_offers == offers) {
        // offers had been set already by a previous visit
        // Wl side is already configured
        setDragTarget();
        return;
    }

    // TODO: make sure that offers are not changed in between visits

    m_offers = offers;

    for (auto const& mimePair : offers) {
        data_source->offer(mimePair.first.toStdString());
    }

    setDragTarget();
}

void XToWlDrag::setDragTarget()
{
    auto ac = m_visit->target();
    workspace()->activateClient(ac);
    waylandServer()->seat()->drags().set_target(ac->surface(), ac->input_transform());
}

bool XToWlDrag::checkForFinished()
{
    if (!m_visit) {
        // not dropped above Wl native target
        Q_EMIT finish(this);
        return true;
    }

    if (!m_visit->finished()) {
        return false;
    }

    if (m_dataRequests.size() == 0) {
        // need to wait for first data request
        return false;
    }

    auto transfersFinished
        = std::all_of(m_dataRequests.begin(),
                      m_dataRequests.end(),
                      [](QPair<xcb_timestamp_t, bool> req) { return req.second; });

    if (transfersFinished) {
        m_visit->sendFinished();
        Q_EMIT finish(this);
    }
    return transfersFinished;
}

bool XToWlDrag::end()
{
    dnd->data.source_int = nullptr;
    return false;
}

WlVisit::WlVisit(Toplevel* target, XToWlDrag* drag)
    : QObject()
    , m_target(target)
    , m_drag(drag)
{
    auto xcbConn = drag->dnd->data.x11.connection;

    m_window = xcb_generate_id(xcbConn);
    overwrite_requestor_window(drag->dnd, m_window);

    uint32_t const dndValues[]
        = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_create_window(xcbConn,
                      XCB_COPY_FROM_PARENT,
                      m_window,
                      kwinApp()->x11RootWindow(),
                      0,
                      0,
                      8192,
                      8192, // TODO: get current screen size and connect to changes
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      drag->dnd->data.x11.screen->root_visual,
                      XCB_CW_EVENT_MASK,
                      dndValues);

    uint32_t version = Dnd::version();
    xcb_change_property(xcbConn,
                        XCB_PROP_MODE_REPLACE,
                        m_window,
                        atoms->xdnd_aware,
                        XCB_ATOM_ATOM,
                        32,
                        1,
                        &version);

    xcb_map_window(xcbConn, m_window);
    workspace()->stacking_order->add_manual_overlay(m_window);
    workspace()->stacking_order->update(true);

    xcb_flush(xcbConn);
    m_mapped = true;
}

WlVisit::~WlVisit()
{
    // TODO(romangg): Use the x11_data here. But we must ensure the Dnd object still exists at this
    //                point, i.e. use explicit ownership through smart pointer only.
    auto xcbConn = kwinApp()->x11Connection();
    xcb_destroy_window(xcbConn, m_window);
    xcb_flush(xcbConn);
}

bool WlVisit::leave()
{
    overwrite_requestor_window(m_drag->dnd, XCB_WINDOW_NONE);
    unmapProxyWindow();
    return m_finished;
}

bool WlVisit::handleClientMessage(xcb_client_message_event_t* event)
{
    if (event->window != m_window) {
        return false;
    }

    if (event->type == atoms->xdnd_enter) {
        return handleEnter(event);
    } else if (event->type == atoms->xdnd_position) {
        return handlePosition(event);
    } else if (event->type == atoms->xdnd_drop) {
        return handleDrop(event);
    } else if (event->type == atoms->xdnd_leave) {
        return handleLeave(event);
    }
    return false;
}

static bool hasMimeName(Mimes const& mimes, QString const& name)
{
    return std::any_of(
        mimes.begin(), mimes.end(), [name](auto const& m) { return m.first == name; });
}

using Mime = QPair<QString, xcb_atom_t>;

bool WlVisit::handleEnter(xcb_client_message_event_t* event)
{
    if (m_entered) {
        // A drag already entered.
        return true;
    }

    m_entered = true;

    auto data = &event->data;
    m_srcWindow = data->data32[0];
    m_version = data->data32[1] >> 24;

    // get types
    Mimes offers;
    if (!(data->data32[1] & 1)) {
        // message has only max 3 types (which are directly in data)
        for (size_t i = 0; i < 3; i++) {
            xcb_atom_t mimeAtom = data->data32[2 + i];
            auto const mimeStrings = atomToMimeTypes(mimeAtom);
            for (auto const& mime : mimeStrings) {
                if (!hasMimeName(offers, mime)) {
                    offers << Mime(mime, mimeAtom);
                }
            }
        }
    } else {
        // more than 3 types -> in window property
        getMimesFromWinProperty(offers);
    }

    Q_EMIT offersReceived(offers);
    return true;
}

void WlVisit::getMimesFromWinProperty(Mimes& offers)
{
    auto xcbConn = m_drag->dnd->data.x11.connection;
    auto cookie = xcb_get_property(
        xcbConn, 0, m_srcWindow, atoms->xdnd_type_list, XCB_GET_PROPERTY_TYPE_ANY, 0, 0x1fffffff);

    auto reply = xcb_get_property_reply(xcbConn, cookie, nullptr);
    if (reply == nullptr) {
        return;
    }
    if (reply->type != XCB_ATOM_ATOM || reply->value_len == 0) {
        // invalid reply value
        free(reply);
        return;
    }

    auto mimeAtoms = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
    for (size_t i = 0; i < reply->value_len; ++i) {
        auto const mimeStrings = atomToMimeTypes(mimeAtoms[i]);
        for (auto const& mime : mimeStrings) {
            if (!hasMimeName(offers, mime)) {
                offers << Mime(mime, mimeAtoms[i]);
            }
        }
    }
    free(reply);
}

bool WlVisit::handlePosition(xcb_client_message_event_t* event)
{
    auto data = &event->data;
    m_srcWindow = data->data32[0];

    if (!m_target) {
        // not over Wl window at the moment
        m_action = DnDAction::none;
        m_actionAtom = XCB_ATOM_NONE;
        sendStatus();
        return true;
    }

    auto const pos = data->data32[2];
    Q_UNUSED(pos);

    xcb_timestamp_t const timestamp = data->data32[3];
    m_drag->x11Source()->setTimestamp(timestamp);

    xcb_atom_t actionAtom = m_version > 1 ? data->data32[4] : atoms->xdnd_action_copy;
    auto action = Drag::atomToClientAction(actionAtom);

    if (action == DnDAction::none) {
        // copy action is always possible in XDND
        action = DnDAction::copy;
        actionAtom = atoms->xdnd_action_copy;
    }

    if (m_action != action) {
        m_action = action;
        m_actionAtom = actionAtom;
        m_drag->setDragAndDropAction(m_action);
    }

    sendStatus();
    return true;
}

bool WlVisit::handleDrop(xcb_client_message_event_t* event)
{
    m_dropHandled = true;

    auto data = &event->data;
    m_srcWindow = data->data32[0];
    xcb_timestamp_t const timestamp = data->data32[2];
    m_drag->x11Source()->setTimestamp(timestamp);

    // We do nothing more here, the drop is being processed through the X11Source object.
    doFinish();
    return true;
}

void WlVisit::doFinish()
{
    m_finished = true;
    unmapProxyWindow();
    Q_EMIT finish(this);
}

bool WlVisit::handleLeave(xcb_client_message_event_t* event)
{
    m_entered = false;
    auto data = &event->data;
    m_srcWindow = data->data32[0];
    doFinish();
    return true;
}

void WlVisit::sendStatus()
{
    // Receive position events.
    uint32_t flags = 1 << 1;
    if (targetAcceptsAction()) {
        // accept the drop
        flags |= (1 << 0);
    }

    xcb_client_message_data_t data = {{0}};
    data.data32[0] = m_window;
    data.data32[1] = flags;
    data.data32[4] = flags & (1 << 0) ? m_actionAtom : static_cast<uint32_t>(XCB_ATOM_NONE);

    Drag::sendClientMessage(m_srcWindow, atoms->xdnd_status, &data);
}

void WlVisit::sendFinished()
{
    auto const accepted = m_entered && m_action != DnDAction::none;

    xcb_client_message_data_t data = {{0}};
    data.data32[0] = m_window;
    data.data32[1] = accepted;
    data.data32[2] = accepted ? m_actionAtom : static_cast<uint32_t>(XCB_ATOM_NONE);

    Drag::sendClientMessage(m_srcWindow, atoms->xdnd_finished, &data);
}

bool WlVisit::targetAcceptsAction() const
{
    if (m_action == DnDAction::none) {
        return false;
    }
    auto const selAction = m_drag->selectedDragAndDropAction();
    return selAction == m_action || selAction == DnDAction::copy;
}

void WlVisit::unmapProxyWindow()
{
    if (!m_mapped) {
        return;
    }

    auto xcbConn = m_drag->dnd->data.x11.connection;
    xcb_unmap_window(xcbConn, m_window);

    workspace()->stacking_order->remove_manual_overlay(m_window);
    workspace()->stacking_order->update(true);

    xcb_flush(xcbConn);
    m_mapped = false;
}

}
