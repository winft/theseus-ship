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
#include "wl_visit.h"

#include "base/wayland/server.h"
#include "toplevel.h"
#include "win/activation.h"
#include "win/space.h"

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <QTimer>

namespace KWin::xwl
{

x11_drag::x11_drag(x11_source_ext& source)
    : source{source}
{
    QObject::connect(source.get_qobject(),
                     &q_x11_source::transfer_ready,
                     qobject.get(),
                     [this](xcb_atom_t target, qint32 fd) {
                         Q_UNUSED(target);
                         Q_UNUSED(fd);
                         data_requests.emplace_back(this->source.timestamp, false);
                     });

    QObject::connect(
        source.get_source(), &data_source_ext::accepted, qobject.get(), [this](auto /*mime_type*/) {
            // TODO(romangg): handle?
        });
    QObject::connect(source.get_source(), &data_source_ext::dropped, qobject.get(), [this] {
        if (visit) {
            QObject::connect(visit->qobject.get(),
                             &wl_visit_qobject::finish,
                             qobject.get(),
                             [this, visit = visit.get()] {
                                 Q_UNUSED(visit);
                                 check_for_finished();
                             });

            QTimer::singleShot(2000, qobject.get(), [this] {
                if (!visit->state.entered || !visit->state.drop_handled) {
                    // X client timed out
                    Q_EMIT qobject->finish();
                } else if (data_requests.size() == 0) {
                    // Wl client timed out
                    visit->send_finished();
                    Q_EMIT qobject->finish();
                }
            });
        }
        check_for_finished();
    });
    QObject::connect(source.get_source(), &data_source_ext::finished, qobject.get(), [this] {
        // this call is not reliably initiated by Wayland clients
        check_for_finished();
    });
}

x11_drag::~x11_drag() = default;

drag_event_reply x11_drag::move_filter(Toplevel* target, QPoint const& pos)
{
    Q_UNUSED(pos);

    auto seat = waylandServer()->seat();

    if (visit && visit->target == target) {
        // still same Wl target, wait for X events
        return drag_event_reply::ignore;
    }

    auto const had_visit = static_cast<bool>(visit);
    if (visit) {
        if (visit->leave()) {
            visit.reset();
        } else {
            QObject::connect(visit->qobject.get(),
                             &wl_visit_qobject::finish,
                             qobject.get(),
                             [this, visit = visit.get()] {
                                 remove_all_if(old_visits,
                                               [visit](auto&& old) { return old.get() == visit; });
                             });
            old_visits.emplace_back(visit.release());
        }
    }

    if (!target || !target->surface
        || target->surface->client() == waylandServer()->xwayland_connection()) {
        // Currently there is no target or target is an Xwayland window.
        // Handled here and by X directly.
        if (target && target->surface && target->control) {
            if (source.x11.space->active_client != target) {
                win::activate_window(*source.x11.space, target);
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

    QObject::connect(visit->qobject.get(),
                     &wl_visit_qobject::offers_received,
                     qobject.get(),
                     [this](auto const& offers) { set_offers(offers); });
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
    auto ac = visit->target;
    win::activate_window(*source.x11.space, ac);
    waylandServer()->seat()->drags().set_target(ac->surface, ac->input_transform());
}

bool x11_drag::check_for_finished()
{
    if (!visit) {
        // not dropped above Wl native target
        Q_EMIT qobject->finish();
        return true;
    }

    if (!visit->state.finished) {
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
        Q_EMIT qobject->finish();
    }
    return transfersFinished;
}

}
