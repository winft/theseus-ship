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
#pragma once

#include "drag_wl.h"
#include "drag_x.h"
#include "event_x11.h"
#include "selection_data.h"

#include "base/wayland/server.h"

#include <QPoint>
#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>
#include <memory>
#include <vector>
#include <xcb/xcb.h>

namespace KWin::xwl
{

/**
 * Represents the drag and drop mechanism, on X side this is the XDND protocol.
 * For more information on XDND see: https://johnlindal.wixsite.com/xdnd
 */
template<typename Window>
class drag_and_drop
{
public:
    selection_data<Wrapland::Server::data_source, data_source_ext> data;

    std::unique_ptr<wl_drag<Window>> wldrag;
    std::unique_ptr<x11_drag<Window>> xdrag;
    std::vector<std::unique_ptr<drag<Window>>> old_drags;

    drag_and_drop(runtime const& core)
    {
        data = create_selection_data<Wrapland::Server::data_source, data_source_ext>(
            core.x11.atoms->xdnd_selection, core);

        // TODO(romangg): for window size get current screen size and connect to changes.
        register_x11_selection(this, QSize(8192, 8192));
        register_xfixes(this);

        auto xcb_con = kwinApp()->x11Connection();
        xcb_change_property(xcb_con,
                            XCB_PROP_MODE_REPLACE,
                            data.window,
                            core.x11.atoms->xdnd_aware,
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            &drag_and_drop_version);
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

    drag_event_reply drag_move_filter(Window* target, QPoint const& pos)
    {
        // This filter only is used when a drag is in process.
        if (wldrag) {
            return wldrag->move_filter(target, pos);
        }
        if (xdrag) {
            auto reply = xdrag->move_filter(target, pos);

            // Adapt the requestor window if a visit is ongoing. Otherwise reset it to our own
            // window.
            data.requestor_window = xdrag->visit ? xdrag->visit->window : data.window;
            return reply;
        }
        assert(false);
        return drag_event_reply();
    }

    void handle_x11_offer_change(std::vector<std::string> const& /*added*/,
                                 std::vector<std::string> const& /*removed*/)
    {
        // Handled internally.
    }

    bool handle_client_message(xcb_client_message_event_t* event)
    {
        for (auto& drag : old_drags) {
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

        if (handle(wldrag) || handle(xdrag)) {
            return true;
        }
        return false;
    }

    void do_handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event)
    {
        if (xdrag) {
            // X drag is in progress, rogue X client took over the selection.
            return;
        }
        if (wldrag) {
            // Wl drag is in progress - don't overwrite by rogue X client,
            // get it back instead!
            own_selection(this, true);
            return;
        }

        data.x11_source.reset();

        auto const seat = waylandServer()->seat();
        auto originSurface = seat->pointers().get_focus().surface;
        if (!originSurface) {
            return;
        }

        if (originSurface->client() != waylandServer()->xwayland_connection()) {
            // focused surface client is not Xwayland - do not allow drag to start
            // TODO: can we make this stronger (window id comparison)?
            return;
        }
        if (!seat->pointers().is_button_pressed(Qt::LeftButton)) {
            // we only allow drags to be started on (left) pointer button being
            // pressed for now
            return;
        }

        create_x11_source(this, event);
        if (!data.x11_source) {
            return;
        }

        assert(!data.source_int);
        data.source_int.reset(new data_source_ext);
        data.x11_source->set_source(data.source_int.get());

        xdrag = std::make_unique<x11_drag<Toplevel>>(*data.x11_source);

        QObject::connect(
            data.qobject.get(),
            &q_selection::transfer_finished,
            xdrag->qobject.get(),
            [xdrag = xdrag.get()](auto time) { xdrag->handle_transfer_finished(time); });

        // Start drag with serial of last left pointer button press.
        // This means X to Wl drags can only be executed with the left pointer button being pressed.
        // For touch and (maybe) other pointer button drags we have to revisit this.
        //
        // Until then we accept the restriction for Xwayland clients.
        seat->drags().start(data.source_int->src(),
                            originSurface,
                            nullptr,
                            seat->pointers().button_serial(Qt::LeftButton));
        seat->drags().set_source_client_movement_blocked(false);
    }

private:
    // start and end Wl native client drags (Wl -> Xwl)
    void start_drag()
    {
        auto srv_src = waylandServer()->seat()->drags().get_source().src;

        if (xdrag) {
            // X to Wl drag, started by us, is in progress.
            return;
        }

        // There can only ever be one Wl native drag at the same time.
        assert(!wldrag);

        // New Wl to X drag, init drag and Wl source.
        auto source = new wl_source<Wrapland::Server::data_source>(srv_src, data.core);
        wldrag = std::make_unique<wl_drag<Toplevel>>(*source, data.window);
        set_wl_source(this, source);
        own_selection(this, true);
    }

    void end_drag()
    {
        auto process = [this](auto& drag) {
            if (drag->end()) {
                drag.reset();
            } else {
                QObject::connect(drag->qobject.get(),
                                 &drag_qobject::finish,
                                 data.qobject.get(),
                                 [this, drag = drag.get()] { clear_old_drag(drag); });
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

    void clear_old_drag(xwl::drag<Window>* drag)
    {
        remove_all_if(old_drags, [drag](auto&& old) { return old.get() == drag; });
    }
};

}
