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

#include "drag.h"
#include "mime.h"
#include "sources.h"
#include "sources_ext.h"
#include "types.h"
#include "wl_visit.h"

#include "base/wayland/server.h"
#include "win/activation.h"
#include "win/space.h"

#include <QPoint>
#include <QTimer>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

namespace KWin::xwl
{

template<typename Window>
class x11_drag : public drag<Window>
{
public:
    explicit x11_drag(x11_source<data_source_ext, Window>& source)
        : source{source}
    {
        QObject::connect(source.get_qobject(),
                         &q_x11_source::transfer_ready,
                         this->qobject.get(),
                         [this](xcb_atom_t target, qint32 fd) {
                             Q_UNUSED(target);
                             Q_UNUSED(fd);
                             data_requests.emplace_back(this->source.timestamp, false);
                         });

        QObject::connect(source.get_source(),
                         &data_source_ext::accepted,
                         this->qobject.get(),
                         [](auto /*mime_type*/) {
                             // TODO(romangg): handle?
                         });
        QObject::connect(
            source.get_source(), &data_source_ext::dropped, this->qobject.get(), [this] {
                if (visit) {
                    QObject::connect(visit->qobject.get(),
                                     &wl_visit_qobject::finish,
                                     this->qobject.get(),
                                     [this, visit = visit.get()] {
                                         Q_UNUSED(visit);
                                         check_for_finished();
                                     });

                    QTimer::singleShot(2000, this->qobject.get(), [this] {
                        if (!visit->state.entered || !visit->state.drop_handled) {
                            // X client timed out
                            Q_EMIT this->qobject->finish();
                        } else if (data_requests.size() == 0) {
                            // Wl client timed out
                            visit->send_finished();
                            Q_EMIT this->qobject->finish();
                        }
                    });
                }
                check_for_finished();
            });
        QObject::connect(
            source.get_source(), &data_source_ext::finished, this->qobject.get(), [this] {
                // this call is not reliably initiated by Wayland clients
                check_for_finished();
            });
    }

    drag_event_reply move_filter(Window* target, QPoint const& pos) override
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
                                 this->qobject.get(),
                                 [this, visit = visit.get()] {
                                     remove_all_if(old_visits, [visit](auto&& old) {
                                         return old.get() == visit;
                                     });
                                 });
                old_visits.emplace_back(visit.release());
            }
        }

        if (!target || !target->surface
            || target->surface->client() == waylandServer()->xwayland_connection()) {
            // Currently there is no target or target is an Xwayland window.
            // Handled here and by X directly.
            if (target && target->surface && target->control) {
                if (source.core.space->stacking.active != target) {
                    win::activate_window(*source.core.space, target);
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
                         this->qobject.get(),
                         [this](auto const& offers) { set_offers(offers); });
        return drag_event_reply::ignore;
    }

    bool handle_client_message(xcb_client_message_event_t* event) override
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

    bool end() override
    {
        return false;
    }

    void handle_transfer_finished(xcb_timestamp_t time)
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

    std::unique_ptr<data_source_ext> data_source;
    std::unique_ptr<wl_visit<Window>> visit;

private:
    void set_offers(mime_atoms const& offers)
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

    void set_drag_target()
    {
        auto ac = visit->target;
        win::activate_window(*source.core.space, ac);
        waylandServer()->seat()->drags().set_target(ac->surface, win::get_input_transform(*ac));
    }

    bool check_for_finished()
    {
        if (!visit) {
            // not dropped above Wl native target
            Q_EMIT this->qobject->finish();
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
            Q_EMIT this->qobject->finish();
        }
        return transfersFinished;
    }

    x11_source<data_source_ext, Window>& source;
    mime_atoms offers;
    std::vector<std::pair<xcb_timestamp_t, bool>> data_requests;

    std::vector<std::unique_ptr<wl_visit<Window>>> old_visits;
};

}
