/*
SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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

    drag_event_reply move_filter(std::optional<typename Space::window_t> target,
                                 QPoint const& pos) override
    {
        using var_win = typename Space::window_t;
        auto seat = source.core.space->base.server->seat();

        if (visit && var_win(visit->target) == target) {
            // no target change
            return drag_event_reply::take;
        }

        // Leave current target.
        if (visit) {
            seat->drags().set_target(nullptr);
            visit->leave();
            visit.reset();
        }

        if (!target) {
            return drag_event_reply::wayland;
        }

        return std::visit(overload{[&](typename Space::x11_window* win) {
                                       // We have a new target.
                                       win::activate_window(*source.core.space, *win);
                                       seat->drags().set_target(
                                           win->surface, pos, win::get_input_transform(*win));

                                       visit.reset(new x11_visit(win, source, proxy_window));
                                       return drag_event_reply::take;
                                   },
                                   [](auto&&) { return drag_event_reply::wayland; }},
                          *target);
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
