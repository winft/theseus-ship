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
#include "x11_visit.h"

#include "base/wayland/server.h"
#include "win/activation.h"
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

    if (visit && visit->target == target) {
        // no target change
        return drag_event_reply::take;
    }

    // Leave current target.
    if (visit) {
        seat->drags().set_target(nullptr);
        visit->leave();
        visit.reset();
    }

    if (!dynamic_cast<win::x11::window*>(target)) {
        // no target or wayland native target,
        // handled by input code directly
        return drag_event_reply::wayland;
    }

    // We have a new target.

    win::activate_window(*source.x11.space, target);
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
    if (!visit || visit->state.finished) {
        visit.reset();
        return true;
    }

    connect(visit->qobject.get(), &x11_visit_qobject::finish, this, [this, visit = visit.get()] {
        Q_ASSERT(this->visit.get() == visit);
        this->visit.reset();

        // We directly allow to delete previous visits.
        Q_EMIT finish(this);
    });
    return false;
}

}
