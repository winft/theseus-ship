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
#include "mime.h"
#include "sources.h"
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

x11_drag::x11_drag(x11_source_ext& source)
    : source{source}
{
    connect(source.get_qobject(),
            &q_x11_source::transfer_ready,
            this,
            [this](xcb_atom_t target, qint32 fd) {
                Q_UNUSED(target);
                Q_UNUSED(fd);
                data_requests.emplace_back(this->source.timestamp, false);
            });

    QObject::connect(
        source.get_source(), &data_source_ext::accepted, this, [this](auto /*mime_type*/) {
            // TODO(romangg): handle?
        });
    QObject::connect(source.get_source(), &data_source_ext::dropped, this, [this] {
        if (visit) {
            connect(visit.get(), &wl_visit::finish, this, [this](wl_visit* visit) {
                Q_UNUSED(visit);
                check_for_finished();
            });

            QTimer::singleShot(2000, this, [this] {
                if (!visit->get_entered() || !visit->get_drop_handled()) {
                    // X client timed out
                    Q_EMIT finish(this);
                } else if (data_requests.size() == 0) {
                    // Wl client timed out
                    visit->send_finished();
                    Q_EMIT finish(this);
                }
            });
        }
        check_for_finished();
    });
    QObject::connect(source.get_source(), &data_source_ext::finished, this, [this] {
        // this call is not reliably initiated by Wayland clients
        check_for_finished();
    });
}

x11_drag::~x11_drag() = default;

drag_event_reply x11_drag::move_filter(Toplevel* target, QPoint const& pos)
{
    Q_UNUSED(pos);

    auto seat = waylandServer()->seat();

    if (visit && visit->get_target() == target) {
        // still same Wl target, wait for X events
        return drag_event_reply::ignore;
    }

    auto const had_visit = static_cast<bool>(visit);
    if (visit) {
        if (visit->leave()) {
            visit.reset();
        } else {
            connect(visit.get(), &wl_visit::finish, this, [this](wl_visit* visit) {
                remove_all_if(old_visits, [visit](auto&& old) { return old.get() == visit; });
            });
            old_visits.emplace_back(visit.release());
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
    visit.reset(new wl_visit(target, source));

    connect(visit.get(), &wl_visit::offers_received, this, &x11_drag::set_offers);
    return drag_event_reply::ignore;
}

bool x11_drag::handle_client_message(xcb_client_message_event_t* event)
{
    for (auto const& visit : old_visits) {
        if (visit->handle_client_message(event)) {
            return true;
        }
    }

    if (visit && visit->handle_client_message(event)) {
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
    auto it = std::find_if(data_requests.begin(), data_requests.end(), [time](auto const& req) {
        return req.first == time && req.second == false;
    });
    if (it == data_requests.end()) {
        // Transfer finished for a different drag.
        return;
    }
    (*it).second = true;
    check_for_finished();
}

void x11_drag::set_offers(mime_atoms const& offers)
{
    source.offers = offers;

    if (offers.empty()) {
        // There are no offers, so just directly set the drag target,
        // no transfer possible anyways.
        set_drag_target();
        return;
    }

    if (this->offers == offers) {
        // offers had been set already by a previous visit
        // Wl side is already configured
        set_drag_target();
        return;
    }

    // TODO: make sure that offers are not changed in between visits

    this->offers = offers;

    for (auto const& mimePair : offers) {
        source.get_source()->offer(mimePair.id);
    }

    set_drag_target();
}

void x11_drag::set_drag_target()
{
    auto ac = visit->get_target();
    workspace()->activateClient(ac);
    waylandServer()->seat()->drags().set_target(ac->surface(), ac->input_transform());
}

bool x11_drag::check_for_finished()
{
    if (!visit) {
        // not dropped above Wl native target
        Q_EMIT finish(this);
        return true;
    }

    if (!visit->get_finished()) {
        return false;
    }

    if (data_requests.size() == 0) {
        // need to wait for first data request
        return false;
    }

    auto transfersFinished = std::all_of(
        data_requests.begin(), data_requests.end(), [](auto const& req) { return req.second; });

    if (transfersFinished) {
        visit->send_finished();
        Q_EMIT finish(this);
    }
    return transfersFinished;
}

wl_visit::wl_visit(Toplevel* target, x11_source_ext& source)
    : target{target}
    , source{source}
{
    auto xcb_con = source.x11.connection;

    window = xcb_generate_id(xcb_con);
    uint32_t const dndValues[]
        = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};

    xcb_create_window(xcb_con,
                      XCB_COPY_FROM_PARENT,
                      window,
                      kwinApp()->x11RootWindow(),
                      0,
                      0,
                      8192,
                      8192, // TODO: get current screen size and connect to changes
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      source.x11.screen->root_visual,
                      XCB_CW_EVENT_MASK,
                      dndValues);

    uint32_t version = drag_and_drop::version();
    xcb_change_property(
        xcb_con, XCB_PROP_MODE_REPLACE, window, atoms->xdnd_aware, XCB_ATOM_ATOM, 32, 1, &version);

    xcb_map_window(xcb_con, window);
    workspace()->stacking_order->add_manual_overlay(window);
    workspace()->stacking_order->update(true);

    xcb_flush(xcb_con);
    mapped = true;
}

wl_visit::~wl_visit()
{
    // TODO(romangg): Use the x11_data here. But we must ensure the Dnd object still exists at this
    //                point, i.e. use explicit ownership through smart pointer only.
    xcb_destroy_window(source.x11.connection, window);
    xcb_flush(source.x11.connection);
}

bool wl_visit::leave()
{
    unmap_proxy_window();
    return finished;
}

bool wl_visit::handle_client_message(xcb_client_message_event_t* event)
{
    if (event->window != window) {
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
    if (entered) {
        // A drag already entered.
        return true;
    }

    entered = true;

    auto data = &event->data;
    source_window = data->data32[0];
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
    auto cookie = xcb_get_property(source.x11.connection,
                                   0,
                                   source_window,
                                   atoms->xdnd_type_list,
                                   XCB_GET_PROPERTY_TYPE_ANY,
                                   0,
                                   0x1fffffff);

    auto reply = xcb_get_property_reply(source.x11.connection, cookie, nullptr);
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
    source_window = data->data32[0];

    if (!target) {
        // not over Wl window at the moment
        this->action = dnd_action::none;
        action_atom = XCB_ATOM_NONE;
        send_status();
        return true;
    }

    auto const pos = data->data32[2];
    Q_UNUSED(pos);

    source.timestamp = data->data32[3];

    xcb_atom_t actionAtom = m_version > 1 ? data->data32[4] : atoms->xdnd_action_copy;
    auto action = atom_to_client_action(actionAtom);

    if (action == dnd_action::none) {
        // copy action is always possible in XDND
        action = dnd_action::copy;
        actionAtom = atoms->xdnd_action_copy;
    }

    if (this->action != action) {
        this->action = action;
        action_atom = actionAtom;
        source.get_source()->set_actions(action);
    }

    send_status();
    return true;
}

bool wl_visit::handle_drop(xcb_client_message_event_t* event)
{
    drop_handled = true;

    auto data = &event->data;
    source_window = data->data32[0];
    source.timestamp = data->data32[2];

    // We do nothing more here, the drop is being processed through the x11_source object.
    do_finish();
    return true;
}

void wl_visit::do_finish()
{
    finished = true;
    unmap_proxy_window();
    Q_EMIT finish(this);
}

bool wl_visit::handle_leave(xcb_client_message_event_t* event)
{
    entered = false;
    auto data = &event->data;
    source_window = data->data32[0];
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
    data.data32[0] = window;
    data.data32[1] = flags;
    data.data32[4] = flags & (1 << 0) ? action_atom : static_cast<uint32_t>(XCB_ATOM_NONE);

    send_client_message(source.x11.connection, source_window, atoms->xdnd_status, &data);
}

void wl_visit::send_finished()
{
    auto const accepted = entered && action != dnd_action::none;

    xcb_client_message_data_t data = {{0}};
    data.data32[0] = window;
    data.data32[1] = accepted;
    data.data32[2] = accepted ? action_atom : static_cast<uint32_t>(XCB_ATOM_NONE);

    send_client_message(source.x11.connection, source_window, atoms->xdnd_finished, &data);
}

bool wl_visit::target_accepts_action() const
{
    if (action == dnd_action::none) {
        return false;
    }
    auto const src_action = source.get_source()->action;
    return src_action == action || src_action == dnd_action::copy;
}

void wl_visit::unmap_proxy_window()
{
    if (!mapped) {
        return;
    }

    xcb_unmap_window(source.x11.connection, window);

    workspace()->stacking_order->remove_manual_overlay(window);
    workspace()->stacking_order->update(true);

    xcb_flush(source.x11.connection);
    mapped = false;
}

}
