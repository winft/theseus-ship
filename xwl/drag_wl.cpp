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
#include "drag_wl.h"

#include "dnd.h"
#include "selection.h"

#include "atoms.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/x11/window.h"

#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>

namespace KWin::Xwl
{

WlToXDrag::WlToXDrag(Wrapland::Server::data_source* source, xcb_window_t proxy_window)
    : source{source}
    , proxy_window{proxy_window}
{
}

DragEventReply WlToXDrag::move_filter(Toplevel* target, QPoint const& pos)
{
    auto seat = waylandServer()->seat();

    if (m_visit && m_visit->target() == target) {
        // no target change
        return DragEventReply::Take;
    }

    // Leave current target.
    if (m_visit) {
        seat->drags().set_target(nullptr);
        m_visit->leave();
        m_visit.reset();
    }

    if (!qobject_cast<win::x11::window*>(target)) {
        // no target or wayland native target,
        // handled by input code directly
        return DragEventReply::Wayland;
    }

    // We have a new target.

    workspace()->activateClient(target, false);
    seat->drags().set_target(target->surface(), pos, target->input_transform());

    m_visit.reset(new Xvisit(target, source, proxy_window));
    return DragEventReply::Take;
}

bool WlToXDrag::handle_client_message(xcb_client_message_event_t* event)
{
    if (m_visit && m_visit->handle_client_message(event)) {
        return true;
    }
    return false;
}

bool WlToXDrag::end()
{
    if (!m_visit || m_visit->finished()) {
        m_visit.reset();
        return true;
    }

    connect(m_visit.get(), &Xvisit::finish, this, [this](Xvisit* visit) {
        Q_ASSERT(m_visit.get() == visit);
        m_visit.reset();

        // We directly allow to delete previous visits.
        Q_EMIT finish(this);
    });
    return false;
}

Xvisit::Xvisit(Toplevel* target, Wrapland::Server::data_source* source, xcb_window_t drag_window)
    : QObject()
    , m_target(target)
    , source{source}
    , drag_window{drag_window}
{
    // first check supported DND version
    auto xcbConn = kwinApp()->x11Connection();
    auto cookie = xcb_get_property(
        xcbConn, 0, m_target->xcb_window(), atoms->xdnd_aware, XCB_GET_PROPERTY_TYPE_ANY, 0, 1);

    auto reply = xcb_get_property_reply(xcbConn, cookie, nullptr);
    if (!reply) {
        do_finish();
        return;
    }

    if (reply->type != XCB_ATOM_ATOM) {
        do_finish();
        free(reply);
        return;
    }

    auto value = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
    m_version = qMin(*value, Dnd::version());
    if (m_version < 1) {
        // minimal version we accept is 1
        do_finish();
        free(reply);
        return;
    }
    free(reply);

    // proxy drop
    receive_offer();

    notifiers.drop = connect(
        waylandServer()->seat(), &Wrapland::Server::Seat::dragEnded, this, [this](auto success) {
            if (success) {
                drop();
            } else {
                leave();
            }
        });
}

bool Xvisit::handle_client_message(xcb_client_message_event_t* event)
{
    if (event->type == atoms->xdnd_status) {
        return handle_status(event);
    } else if (event->type == atoms->xdnd_finished) {
        return handle_finished(event);
    }
    return false;
}

bool Xvisit::handle_status(xcb_client_message_event_t* event)
{
    auto data = &event->data;
    if (data->data32[0] != m_target->xcb_window()) {
        // wrong target window
        return false;
    }

    m_accepts = data->data32[1] & 1;
    xcb_atom_t actionAtom = data->data32[4];

    // TODO: we could optimize via rectangle in data32[2] and data32[3]

    // position round trip finished
    m_pos.pending = false;

    if (!m_state.dropped) {
        // as long as the drop is not yet done determine requested action
        actions.preferred = Drag::atom_to_client_action(actionAtom);
        update_actions();
    }

    if (m_pos.cached) {
        // send cached position
        m_pos.cached = false;
        send_position(m_pos.cache);
    } else if (m_state.dropped) {
        // drop was done in between, now close it out
        drop();
    }
    return true;
}

bool Xvisit::handle_finished(xcb_client_message_event_t* event)
{
    auto data = &event->data;

    if (data->data32[0] != m_target->xcb_window()) {
        // different target window
        return false;
    }

    if (!m_state.dropped) {
        // drop was never done
        do_finish();
        return true;
    }

    auto const success = m_version > 4 ? data->data32[1] & 1 : true;
    xcb_atom_t const usedActionAtom
        = m_version > 4 ? data->data32[2] : static_cast<uint32_t>(XCB_ATOM_NONE);
    Q_UNUSED(success);
    Q_UNUSED(usedActionAtom);

    do_finish();
    return true;
}

void Xvisit::send_position(QPointF const& globalPos)
{
    int16_t const x = globalPos.x();
    int16_t const y = globalPos.y();

    if (m_pos.pending) {
        m_pos.cache = QPoint(x, y);
        m_pos.cached = true;
        return;
    }

    m_pos.pending = true;

    xcb_client_message_data_t data = {{0}};
    data.data32[0] = drag_window;
    data.data32[2] = (x << 16) | y;
    data.data32[3] = XCB_CURRENT_TIME;
    data.data32[4] = Drag::client_action_to_atom(actions.proposed);

    Drag::send_client_message(m_target->xcb_window(), atoms->xdnd_position, &data);
}

void Xvisit::leave()
{
    Q_ASSERT(!m_state.dropped);
    if (m_state.finished) {
        // Was already finished.
        return;
    }
    // We only need to leave if we entered before.
    if (m_state.entered) {
        send_leave();
    }
    do_finish();
}

void Xvisit::receive_offer()
{
    if (m_state.finished) {
        // Already ended.
        return;
    }

    enter();
    update_actions();

    notifiers.action = connect(source,
                               &Wrapland::Server::data_source::supported_dnd_actions_changed,
                               this,
                               &Xvisit::update_actions);

    send_position(waylandServer()->seat()->pointers().get_position());
}

void Xvisit::enter()
{
    m_state.entered = true;

    // Send enter event and current position to X client.
    send_enter();

    // Proxy future pointer position changes.
    notifiers.motion = connect(waylandServer()->seat(),
                               &Wrapland::Server::Seat::pointerPosChanged,
                               this,
                               &Xvisit::send_position);
}

void Xvisit::send_enter()
{
    xcb_client_message_data_t data = {{0}};
    data.data32[0] = drag_window;
    data.data32[1] = m_version << 24;

    auto const mimeTypesNames = source->mime_types();
    auto const mimesCount = mimeTypesNames.size();
    size_t cnt = 0;
    size_t totalCnt = 0;

    for (auto const& mimeName : mimeTypesNames) {
        // 3 mimes and less can be sent directly in the XdndEnter message
        if (totalCnt == 3) {
            break;
        }
        auto const atom = mime_type_to_atom(mimeName.c_str());

        if (atom != XCB_ATOM_NONE) {
            data.data32[cnt + 2] = atom;
            cnt++;
        }
        totalCnt++;
    }

    for (int i = cnt; i < 3; i++) {
        data.data32[i + 2] = XCB_ATOM_NONE;
    }

    if (mimesCount > 3) {
        // need to first transfer all available mime types
        data.data32[1] |= 1;

        std::vector<xcb_atom_t> targets;
        targets.resize(mimesCount);

        size_t cnt = 0;
        for (auto const& mimeName : mimeTypesNames) {
            auto const atom = mime_type_to_atom(mimeName.c_str());
            if (atom != XCB_ATOM_NONE) {
                targets[cnt] = atom;
                cnt++;
            }
        }

        xcb_change_property(kwinApp()->x11Connection(),
                            XCB_PROP_MODE_REPLACE,
                            drag_window,
                            atoms->xdnd_type_list,
                            XCB_ATOM_ATOM,
                            32,
                            cnt,
                            targets.data());
    }

    Drag::send_client_message(m_target->xcb_window(), atoms->xdnd_enter, &data);
}

void Xvisit::send_drop(uint32_t time)
{
    xcb_client_message_data_t data = {{0}};
    data.data32[0] = drag_window;
    data.data32[2] = time;

    Drag::send_client_message(m_target->xcb_window(), atoms->xdnd_drop, &data);

    if (m_version < 2) {
        do_finish();
    }
}

void Xvisit::send_leave()
{
    xcb_client_message_data_t data = {{0}};
    data.data32[0] = drag_window;

    Drag::send_client_message(m_target->xcb_window(), atoms->xdnd_leave, &data);
}

void Xvisit::update_actions()
{
    auto const old_proposed = actions.proposed;
    auto const supported = source->supported_dnd_actions();

    if (supported.testFlag(actions.preferred)) {
        actions.proposed = actions.preferred;
    } else if (supported.testFlag(DnDAction::copy)) {
        actions.proposed = DnDAction::copy;
    } else {
        actions.proposed = DnDAction::none;
    }

    // Send updated action to X target.
    if (old_proposed != actions.proposed) {
        send_position(waylandServer()->seat()->pointers().get_position());
    }

    auto const pref = actions.preferred != DnDAction::none ? actions.preferred : DnDAction::copy;

    // We assume the X client supports Move, but this might be wrong - then the drag just cancels,
    // if the user tries to force it.
    waylandServer()->seat()->drags().target_actions_update(DnDAction::copy | DnDAction::move, pref);
}

void Xvisit::drop()
{
    Q_ASSERT(!m_state.finished);
    m_state.dropped = true;

    // Stop further updates.
    // TODO(romangg): revisit when we allow ask action
    stop_connections();

    if (!m_state.entered) {
        // wait for enter (init + offers)
        return;
    }
    if (m_pos.pending) {
        // wait for pending position roundtrip
        return;
    }
    if (!m_accepts) {
        // target does not accept current action/offer
        send_leave();
        do_finish();
        return;
    }

    // Dnd session ended successfully.
    send_drop(XCB_CURRENT_TIME);
}

void Xvisit::do_finish()
{
    m_state.finished = true;
    m_pos.cached = false;
    stop_connections();
    Q_EMIT finish(this);
}

void Xvisit::stop_connections()
{
    // Final outcome has been determined from Wayland side, no more updates needed.
    disconnect(notifiers.drop);
    notifiers.drop = QMetaObject::Connection();

    disconnect(notifiers.motion);
    notifiers.motion = QMetaObject::Connection();
    disconnect(notifiers.action);
    notifiers.action = QMetaObject::Connection();
}

}
