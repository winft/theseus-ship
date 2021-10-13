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
#include "types.h"

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

namespace KWin::xwl
{

x11_drag::x11_drag(x11_source_ext* source)
    : m_source{source}
{
    connect(source->qobject(),
            &q_x11_source::transfer_ready,
            this,
            [this](xcb_atom_t target, qint32 fd) {
                Q_UNUSED(target);
                Q_UNUSED(fd);
                m_dataRequests.emplace_back(m_source->timestamp(), false);
            });

    connect(source->source(), &data_source_ext::accepted, this, [this](auto /*mime_type*/) {
        // TODO(romangg): handle?
    });
    connect(source->source(), &data_source_ext::dropped, this, [this] {
        m_performed = true;
        if (m_visit) {
            connect(m_visit.get(), &wl_visit::finish, this, [this](wl_visit* visit) {
                Q_UNUSED(visit);
                check_for_finished();
            });

            QTimer::singleShot(2000, this, [this] {
                if (!m_visit->entered() || !m_visit->drop_handled()) {
                    // X client timed out
                    Q_EMIT finish(this);
                } else if (m_dataRequests.size() == 0) {
                    // Wl client timed out
                    m_visit->send_finished();
                    Q_EMIT finish(this);
                }
            });
        }
        check_for_finished();
    });
    connect(source->source(), &data_source_ext::finished, this, [this] {
        // this call is not reliably initiated by Wayland clients
        check_for_finished();
    });
}

x11_drag::~x11_drag() = default;

drag_event_reply x11_drag::move_filter(Toplevel* target, QPoint const& pos)
{
    Q_UNUSED(pos);

    auto seat = waylandServer()->seat();

    if (m_visit && m_visit->target() == target) {
        // still same Wl target, wait for X events
        return drag_event_reply::ignore;
    }

    auto const had_visit = static_cast<bool>(m_visit);
    if (m_visit) {
        if (m_visit->leave()) {
            m_visit.reset();
        } else {
            connect(m_visit.get(), &wl_visit::finish, this, [this](wl_visit* visit) {
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
        return drag_event_reply::ignore;
    }

    // New Wl native target.
    m_visit.reset(new wl_visit(target, m_source));

    connect(m_visit.get(), &wl_visit::offers_received, this, &x11_drag::set_offers);
    return drag_event_reply::ignore;
}

bool x11_drag::handle_client_message(xcb_client_message_event_t* event)
{
    for (auto const& visit : m_oldVisits) {
        if (visit->handle_client_message(event)) {
            return true;
        }
    }

    if (m_visit && m_visit->handle_client_message(event)) {
        return true;
    }

    return false;
}

bool x11_drag::end()
{
    return false;
}

void x11_drag::handle_transfer_finished(xcb_timestamp_t time)
{
    // We use this mechanism, because the finished call is not reliable done by Wayland clients.
    auto it = std::find_if(m_dataRequests.begin(), m_dataRequests.end(), [time](auto const& req) {
        return req.first == time && req.second == false;
    });
    if (it == m_dataRequests.end()) {
        // Transfer finished for a different drag.
        return;
    }
    (*it).second = true;
    check_for_finished();
}

void x11_drag::set_offers(mime_atoms const& offers)
{
    m_source->set_offers(offers);

    if (offers.empty()) {
        // There are no offers, so just directly set the drag target,
        // no transfer possible anyways.
        set_drag_target();
        return;
    }

    if (m_offers == offers) {
        // offers had been set already by a previous visit
        // Wl side is already configured
        set_drag_target();
        return;
    }

    // TODO: make sure that offers are not changed in between visits

    m_offers = offers;

    for (auto const& mimePair : offers) {
        m_source->source()->offer(mimePair.id);
    }

    set_drag_target();
}

void x11_drag::set_drag_target()
{
    auto ac = m_visit->target();
    workspace()->activateClient(ac);
    waylandServer()->seat()->drags().set_target(ac->surface(), ac->input_transform());
}

bool x11_drag::check_for_finished()
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

    auto transfersFinished = std::all_of(
        m_dataRequests.begin(), m_dataRequests.end(), [](auto const& req) { return req.second; });

    if (transfersFinished) {
        m_visit->send_finished();
        Q_EMIT finish(this);
    }
    return transfersFinished;
}

wl_visit::wl_visit(Toplevel* target, x11_source_ext* source)
    : QObject()
    , m_target(target)
    , source{source}
{
    auto xcbConn = source->x11.connection;

    m_window = xcb_generate_id(xcbConn);
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
                      source->x11.screen->root_visual,
                      XCB_CW_EVENT_MASK,
                      dndValues);

    uint32_t version = drag_and_drop::version();
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

wl_visit::~wl_visit()
{
    // TODO(romangg): Use the x11_data here. But we must ensure the Dnd object still exists at this
    //                point, i.e. use explicit ownership through smart pointer only.
    xcb_destroy_window(source->x11.connection, m_window);
    xcb_flush(source->x11.connection);
}

bool wl_visit::leave()
{
    unmap_proxy_window();
    return m_finished;
}

bool wl_visit::handle_client_message(xcb_client_message_event_t* event)
{
    if (event->window != m_window) {
        return false;
    }

    if (event->type == atoms->xdnd_enter) {
        return handle_enter(event);
    } else if (event->type == atoms->xdnd_position) {
        return handle_position(event);
    } else if (event->type == atoms->xdnd_drop) {
        return handle_drop(event);
    } else if (event->type == atoms->xdnd_leave) {
        return handle_leave(event);
    }
    return false;
}

static bool hasMimeName(mime_atoms const& mimes, std::string const& name)
{
    return std::any_of(mimes.begin(), mimes.end(), [name](auto const& m) { return m.id == name; });
}

bool wl_visit::handle_enter(xcb_client_message_event_t* event)
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
    mime_atoms offers;
    if (!(data->data32[1] & 1)) {
        // message has only max 3 types (which are directly in data)
        for (size_t i = 0; i < 3; i++) {
            xcb_atom_t mimeAtom = data->data32[2 + i];
            auto const mimeStrings = atom_to_mime_types(mimeAtom);
            for (auto const& mime : mimeStrings) {
                if (!hasMimeName(offers, mime)) {
                    offers.emplace_back(mime, mimeAtom);
                }
            }
        }
    } else {
        // more than 3 types -> in window property
        get_mimes_from_win_property(offers);
    }

    Q_EMIT offers_received(offers);
    return true;
}

void wl_visit::get_mimes_from_win_property(mime_atoms& offers)
{
    auto cookie = xcb_get_property(source->x11.connection,
                                   0,
                                   m_srcWindow,
                                   atoms->xdnd_type_list,
                                   XCB_GET_PROPERTY_TYPE_ANY,
                                   0,
                                   0x1fffffff);

    auto reply = xcb_get_property_reply(source->x11.connection, cookie, nullptr);
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
        auto const mimeStrings = atom_to_mime_types(mimeAtoms[i]);
        for (auto const& mime : mimeStrings) {
            if (!hasMimeName(offers, mime)) {
                offers.emplace_back(mime, mimeAtoms[i]);
            }
        }
    }
    free(reply);
}

bool wl_visit::handle_position(xcb_client_message_event_t* event)
{
    auto data = &event->data;
    m_srcWindow = data->data32[0];

    if (!m_target) {
        // not over Wl window at the moment
        m_action = dnd_action::none;
        m_actionAtom = XCB_ATOM_NONE;
        send_status();
        return true;
    }

    auto const pos = data->data32[2];
    Q_UNUSED(pos);

    xcb_timestamp_t const timestamp = data->data32[3];
    source->set_timestamp(timestamp);

    xcb_atom_t actionAtom = m_version > 1 ? data->data32[4] : atoms->xdnd_action_copy;
    auto action = drag::atom_to_client_action(actionAtom);

    if (action == dnd_action::none) {
        // copy action is always possible in XDND
        action = dnd_action::copy;
        actionAtom = atoms->xdnd_action_copy;
    }

    if (m_action != action) {
        m_action = action;
        m_actionAtom = actionAtom;
        source->source()->set_actions(m_action);
    }

    send_status();
    return true;
}

bool wl_visit::handle_drop(xcb_client_message_event_t* event)
{
    m_dropHandled = true;

    auto data = &event->data;
    m_srcWindow = data->data32[0];
    xcb_timestamp_t const timestamp = data->data32[2];
    source->set_timestamp(timestamp);

    // We do nothing more here, the drop is being processed through the x11_source object.
    do_finish();
    return true;
}

void wl_visit::do_finish()
{
    m_finished = true;
    unmap_proxy_window();
    Q_EMIT finish(this);
}

bool wl_visit::handle_leave(xcb_client_message_event_t* event)
{
    m_entered = false;
    auto data = &event->data;
    m_srcWindow = data->data32[0];
    do_finish();
    return true;
}

void wl_visit::send_status()
{
    // Receive position events.
    uint32_t flags = 1 << 1;
    if (target_accepts_action()) {
        // accept the drop
        flags |= (1 << 0);
    }

    xcb_client_message_data_t data = {{0}};
    data.data32[0] = m_window;
    data.data32[1] = flags;
    data.data32[4] = flags & (1 << 0) ? m_actionAtom : static_cast<uint32_t>(XCB_ATOM_NONE);

    drag::send_client_message(m_srcWindow, atoms->xdnd_status, &data);
}

void wl_visit::send_finished()
{
    auto const accepted = m_entered && m_action != dnd_action::none;

    xcb_client_message_data_t data = {{0}};
    data.data32[0] = m_window;
    data.data32[1] = accepted;
    data.data32[2] = accepted ? m_actionAtom : static_cast<uint32_t>(XCB_ATOM_NONE);

    drag::send_client_message(m_srcWindow, atoms->xdnd_finished, &data);
}

bool wl_visit::target_accepts_action() const
{
    if (m_action == dnd_action::none) {
        return false;
    }
    auto const selAction = source->source()->action;
    return selAction == m_action || selAction == dnd_action::copy;
}

void wl_visit::unmap_proxy_window()
{
    if (!m_mapped) {
        return;
    }

    xcb_unmap_window(source->x11.connection, m_window);

    workspace()->stacking_order->remove_manual_overlay(m_window);
    workspace()->stacking_order->update(true);

    xcb_flush(source->x11.connection);
    m_mapped = false;
}

}
