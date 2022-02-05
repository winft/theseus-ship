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
#include "dnd.h"

#include "drag_wl.h"
#include "drag_x.h"
#include "selection_wl.h"
#include "selection_x11.h"

#include "atoms.h"
#include "wayland_server.h"

#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <xcb/xcb.h>

namespace KWin::xwl
{

template<>
void do_handle_xfixes_notify(drag_and_drop* sel, xcb_xfixes_selection_notify_event_t* event)
{
    if (sel->xdrag) {
        // X drag is in progress, rogue X client took over the selection.
        return;
    }
    if (sel->wldrag) {
        // Wl drag is in progress - don't overwrite by rogue X client,
        // get it back instead!
        own_selection(sel, true);
        return;
    }

    sel->data.x11_source.reset();

    auto const seat = waylandServer()->seat();
    auto originSurface = seat->pointers().get_focus().surface;
    if (!originSurface) {
        return;
    }

    if (originSurface->client() != waylandServer()->xWaylandConnection()) {
        // focused surface client is not Xwayland - do not allow drag to start
        // TODO: can we make this stronger (window id comparison)?
        return;
    }
    if (!seat->pointers().is_button_pressed(Qt::LeftButton)) {
        // we only allow drags to be started on (left) pointer button being
        // pressed for now
        return;
    }

    create_x11_source(sel, event);
    if (!sel->data.x11_source) {
        return;
    }

    assert(!sel->data.source_int);
    sel->data.source_int.reset(new data_source_ext);
    sel->data.x11_source->set_source(sel->data.source_int.get());

    sel->xdrag.reset(new x11_drag(*sel->data.x11_source));

    QObject::connect(sel->data.qobject.get(),
                     &q_selection::transfer_finished,
                     sel->xdrag.get(),
                     &x11_drag::handle_transfer_finished);

    // Start drag with serial of last left pointer button press.
    // This means X to Wl drags can only be executed with the left pointer button being pressed.
    // For touch and (maybe) other pointer button drags we have to revisit this.
    //
    // Until then we accept the restriction for Xwayland clients.
    seat->drags().start(sel->data.source_int->src(),
                        originSurface,
                        nullptr,
                        seat->pointers().button_serial(Qt::LeftButton));
    seat->drags().set_source_client_movement_blocked(false);
}

template<>
bool handle_client_message(drag_and_drop* sel, xcb_client_message_event_t* event)
{
    for (auto& drag : sel->old_drags) {
        if (drag->handle_client_message(event)) {
            return true;
        }
    }

    auto handle = [event](auto&& drag) {
        if (!drag) {
            return false;
        }
        return drag->handle_client_message(event);
    };

    if (handle(sel->wldrag) || handle(sel->xdrag)) {
        return true;
    }
    return false;
}

template<>
void handle_x11_offer_change(drag_and_drop* /*sel*/,
                             std::vector<std::string> const& /*added*/,
                             std::vector<std::string> const& /*removed*/)
{
    // Handled internally.
}

// version of DnD support in X
constexpr uint32_t s_version = 5;
uint32_t drag_and_drop::version()
{
    return s_version;
}

drag_and_drop::drag_and_drop(x11_data const& x11)
{
    data = create_selection_data<Wrapland::Server::data_source, data_source_ext>(
        x11.atoms->xdnd_selection, x11);

    // TODO(romangg): for window size get current screen size and connect to changes.
    register_x11_selection(this, QSize(8192, 8192));
    register_xfixes(this);

    auto xcb_con = kwinApp()->x11Connection();
    xcb_change_property(xcb_con,
                        XCB_PROP_MODE_REPLACE,
                        data.window,
                        x11.atoms->xdnd_aware,
                        XCB_ATOM_ATOM,
                        32,
                        1,
                        &s_version);
    xcb_flush(xcb_con);

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::dragStarted,
                     data.qobject.get(),
                     [this]() { start_drag(); });
    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::dragEnded,
                     data.qobject.get(),
                     [this]() { end_drag(); });
}

drag_and_drop::~drag_and_drop() = default;

drag_event_reply drag_and_drop::drag_move_filter(Toplevel* target, QPoint const& pos)
{
    // This filter only is used when a drag is in process.
    if (wldrag) {
        return wldrag->move_filter(target, pos);
    }
    if (xdrag) {
        auto reply = xdrag->move_filter(target, pos);

        // Adapt the requestor window if a visit is ongoing. Otherwise reset it to our own window.
        data.requestor_window = xdrag->visit ? xdrag->visit->get_window() : data.window;
        return reply;
    }
    assert(false);
    return drag_event_reply();
}

void drag_and_drop::start_drag()
{
    auto srv_src = waylandServer()->seat()->drags().get_source().src;

    if (xdrag) {
        // X to Wl drag, started by us, is in progress.
        return;
    }

    // There can only ever be one Wl native drag at the same time.
    assert(!wldrag);

    // New Wl to X drag, init drag and Wl source.
    auto source = new wl_source<Wrapland::Server::data_source>(srv_src, data.x11);
    wldrag.reset(new wl_drag(*source, data.window));
    set_wl_source(this, source);
    own_selection(this, true);
}

void drag_and_drop::end_drag()
{
    auto process = [this](auto& drag) {
        if (drag->end()) {
            drag.reset();
        } else {
            QObject::connect(drag.get(), &drag::finish, data.qobject.get(), [this](auto drag) {
                clear_old_drag(drag);
            });
            old_drags.emplace_back(drag.release());
        }
    };

    if (xdrag) {
        assert(data.source_int);
        xdrag->data_source = std::move(data.source_int);
        process(xdrag);
    } else {
        assert(wldrag);
        process(wldrag);
    }
}

void drag_and_drop::clear_old_drag(xwl::drag* drag)
{
    remove_all_if(old_drags, [drag](auto&& old) { return old.get() == drag; });
}

}
