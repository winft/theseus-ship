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

#include "dnd.h"
#include "drag.h"
#include "sources.h"
#include "x11_visit.h"

#include "base/wayland/server.h"
#include "win/activation.h"

#include <QPoint>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>

namespace KWin::xwl
{

using dnd_actions = Wrapland::Server::dnd_actions;

template<typename Space>
class wl_drag : public drag<Space>
{
public:
    wl_drag(wl_source<Wrapland::Server::data_source, Space> const& source,
            xcb_window_t proxy_window)
        : source{source}
        , proxy_window{proxy_window}
    {
    }

    drag_event_reply move_filter(typename Space::window_t* target, QPoint const& pos) override
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

        auto x11_win = dynamic_cast<typename Space::x11_window*>(target);
        if (!x11_win) {
            // no target or wayland native target,
            // handled by input code directly
            return drag_event_reply::wayland;
        }

        // We have a new target.

        win::activate_window(*source.core.space, *x11_win);
        seat->drags().set_target(x11_win->surface, pos, win::get_input_transform(*x11_win));

        visit.reset(new x11_visit(x11_win, source, proxy_window));
        return drag_event_reply::take;
    }

    bool handle_client_message(xcb_client_message_event_t* event) override
    {
        if (visit && visit->handle_client_message(event)) {
            return true;
        }
        return false;
    }

    bool end() override
    {
        if (!visit || visit->state.finished) {
            visit.reset();
            return true;
        }

        QObject::connect(visit->qobject.get(),
                         &x11_visit_qobject::finish,
                         this->qobject.get(),
                         [this, visit = visit.get()] {
                             Q_ASSERT(this->visit.get() == visit);
                             this->visit.reset();

                             // We directly allow to delete previous visits.
                             Q_EMIT this->qobject->finish();
                         });
        return false;
    }

private:
    wl_source<Wrapland::Server::data_source, Space> const& source;
    xcb_window_t proxy_window;
    std::unique_ptr<x11_visit<Space>> visit;
};

}
