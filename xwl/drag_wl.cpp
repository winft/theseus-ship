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
#include "mime.h"

#include "base/wayland/server.h"
#include "win/space.h"
#include "win/x11/window.h"

#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>

namespace KWin::xwl
{

wl_drag::wl_drag(wl_source<Wrapland::Server::data_source> const& source, xcb_window_t proxy_window)
    : source{source}
    , proxy_window{proxy_window}
{
}

drag_event_reply wl_drag::move_filter(Toplevel* target, QPoint const& pos)
{
    auto seat = waylandServer()->seat();

    if (visit && visit->get_target() == target) {
        // no target change
        return drag_event_reply::take;
    }

    // Leave current target.
    if (visit) {
        seat->drags().set_target(nullptr);
        visit->leave();
        visit.reset();
    }

    if (!qobject_cast<win::x11::window*>(target)) {
        // no target or wayland native target,
        // handled by input code directly
        return drag_event_reply::wayland;
    }

    // We have a new target.

    source.x11.space->activateClient(target, false);
    seat->drags().set_target(target->surface, pos, target->input_transform());

    visit.reset(new x11_visit(target, source, proxy_window));
    return drag_event_reply::take;
}

bool wl_drag::handle_client_message(xcb_client_message_event_t* event)
{
    if (visit && visit->handle_client_message(event)) {
        return true;
    }
    return false;
}

bool wl_drag::end()
{
    if (!visit || visit->finished()) {
        visit.reset();
        return true;
    }

    connect(visit.get(), &x11_visit::finish, this, [this](x11_visit* visit) {
        Q_ASSERT(this->visit.get() == visit);
        this->visit.reset();

        // We directly allow to delete previous visits.
        Q_EMIT finish(this);
    });
    return false;
}

x11_visit::x11_visit(Toplevel* target,
                     wl_source<Wrapland::Server::data_source> const& source,
                     xcb_window_t drag_window)
    : QObject()
    , target(target)
    , source{source}
    , drag_window{drag_window}
{
    // first check supported DND version
    auto xcb_con = source.x11.connection;
    auto cookie = xcb_get_property(xcb_con,
                                   0,
                                   target->xcb_window,
                                   source.x11.space->atoms->xdnd_aware,
                                   XCB_GET_PROPERTY_TYPE_ANY,
                                   0,
                                   1);

    auto reply = xcb_get_property_reply(xcb_con, cookie, nullptr);
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
    version = qMin(*value, drag_and_drop::version());
    if (version < 1) {
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

bool x11_visit::handle_client_message(xcb_client_message_event_t* event)
{
    auto& atoms = source.x11.space->atoms;
    if (event->type == atoms->xdnd_status) {
        return handle_status(event);
    } else if (event->type == atoms->xdnd_finished) {
        return handle_finished(event);
    }
    return false;
}

bool x11_visit::handle_status(xcb_client_message_event_t* event)
{
    auto data = &event->data;
    if (data->data32[0] != target->xcb_window) {
        // wrong target window
        return false;
    }

    m_accepts = data->data32[1] & 1;
    xcb_atom_t actionAtom = data->data32[4];

    // TODO: we could optimize via rectangle in data32[2] and data32[3]

    // position round trip finished
    m_pos.pending = false;

    if (!state.dropped) {
        // as long as the drop is not yet done determine requested action
        actions.preferred = atom_to_client_action(actionAtom, *source.x11.space->atoms);
        update_actions();
    }

    if (m_pos.cached) {
        // send cached position
        m_pos.cached = false;
        send_position(m_pos.cache);
    } else if (state.dropped) {
        // drop was done in between, now close it out
        drop();
    }
    return true;
}

bool x11_visit::handle_finished(xcb_client_message_event_t* event)
{
    auto data = &event->data;

    if (data->data32[0] != target->xcb_window) {
        // different target window
        return false;
    }

    if (!state.dropped) {
        // drop was never done
        do_finish();
        return true;
    }

    auto const success = version > 4 ? data->data32[1] & 1 : true;
    xcb_atom_t const usedActionAtom
        = version > 4 ? data->data32[2] : static_cast<uint32_t>(XCB_ATOM_NONE);
    Q_UNUSED(success);
    Q_UNUSED(usedActionAtom);

    do_finish();
    return true;
}

void x11_visit::send_position(QPointF const& globalPos)
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
    data.data32[4] = client_action_to_atom(actions.proposed, *source.x11.space->atoms);

    send_client_message(
        source.x11.connection, target->xcb_window, source.x11.space->atoms->xdnd_position, &data);
}

void x11_visit::leave()
{
    Q_ASSERT(!state.dropped);
    if (state.finished) {
        // Was already finished.
        return;
    }
    // We only need to leave if we entered before.
    if (state.entered) {
        send_leave();
    }
    do_finish();
}

void x11_visit::receive_offer()
{
    if (state.finished) {
        // Already ended.
        return;
    }

    enter();
    update_actions();

    notifiers.action = connect(source.server_source,
                               &Wrapland::Server::data_source::supported_dnd_actions_changed,
                               this,
                               &x11_visit::update_actions);

    send_position(waylandServer()->seat()->pointers().get_position());
}

void x11_visit::enter()
{
    state.entered = true;

    // Send enter event and current position to X client.
    send_enter();

    // Proxy future pointer position changes.
    notifiers.motion = connect(waylandServer()->seat(),
                               &Wrapland::Server::Seat::pointerPosChanged,
                               this,
                               &x11_visit::send_position);
}

void x11_visit::send_enter()
{
    xcb_client_message_data_t data = {{0}};
    data.data32[0] = drag_window;
    data.data32[1] = version << 24;

    auto const mimeTypesNames = source.server_source->mime_types();
    auto const mimesCount = mimeTypesNames.size();
    size_t cnt = 0;
    size_t totalCnt = 0;

    for (auto const& mimeName : mimeTypesNames) {
        // 3 mimes and less can be sent directly in the XdndEnter message
        if (totalCnt == 3) {
            break;
        }
        auto const atom = mime_type_to_atom(mimeName.c_str(), *source.x11.space->atoms);

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
            auto const atom = mime_type_to_atom(mimeName.c_str(), *source.x11.space->atoms);
            if (atom != XCB_ATOM_NONE) {
                targets[cnt] = atom;
                cnt++;
            }
        }

        xcb_change_property(source.x11.connection,
                            XCB_PROP_MODE_REPLACE,
                            drag_window,
                            source.x11.space->atoms->xdnd_type_list,
                            XCB_ATOM_ATOM,
                            32,
                            cnt,
                            targets.data());
    }

    send_client_message(
        source.x11.connection, target->xcb_window, source.x11.space->atoms->xdnd_enter, &data);
}

void x11_visit::send_drop(uint32_t time)
{
    xcb_client_message_data_t data = {{0}};
    data.data32[0] = drag_window;
    data.data32[2] = time;

    send_client_message(
        source.x11.connection, target->xcb_window, source.x11.space->atoms->xdnd_drop, &data);

    if (version < 2) {
        do_finish();
    }
}

void x11_visit::send_leave()
{
    xcb_client_message_data_t data = {{0}};
    data.data32[0] = drag_window;

    send_client_message(
        source.x11.connection, target->xcb_window, source.x11.space->atoms->xdnd_leave, &data);
}

void x11_visit::update_actions()
{
    auto const old_proposed = actions.proposed;
    auto const supported = source.server_source->supported_dnd_actions();

    if (supported.testFlag(actions.preferred)) {
        actions.proposed = actions.preferred;
    } else if (supported.testFlag(dnd_action::copy)) {
        actions.proposed = dnd_action::copy;
    } else {
        actions.proposed = dnd_action::none;
    }

    // Send updated action to X target.
    if (old_proposed != actions.proposed) {
        send_position(waylandServer()->seat()->pointers().get_position());
    }

    auto const pref = actions.preferred != dnd_action::none ? actions.preferred : dnd_action::copy;

    // We assume the X client supports Move, but this might be wrong - then the drag just
    // cancels, if the user tries to force it.
    waylandServer()->seat()->drags().target_actions_update(
        QFlags({dnd_action::copy, dnd_action::move}), pref);
}

void x11_visit::drop()
{
    Q_ASSERT(!state.finished);
    state.dropped = true;

    // Stop further updates.
    // TODO(romangg): revisit when we allow ask action
    stop_connections();

    if (!state.entered) {
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

void x11_visit::do_finish()
{
    state.finished = true;
    m_pos.cached = false;
    stop_connections();
    Q_EMIT finish(this);
}

void x11_visit::stop_connections()
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
