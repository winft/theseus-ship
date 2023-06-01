/*
SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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

template<typename Space>
class x11_drag : public drag<Space>
{
public:
    explicit x11_drag(x11_source<data_source_ext, Space>& source)
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

    drag_event_reply move_filter(std::optional<typename Space::window_t> target,
                                 QPoint const& pos) override
    {
        Q_UNUSED(pos);

        auto seat = source.core.space->base.server->seat();

        if (visit && typename Space::window_t(visit->target) == target) {
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

        auto unset_target = [&] {
            if (had_visit) {
                // Last received enter event is now void. Wait for the next one.
                seat->drags().set_target(nullptr);
            }
        };

        if (!target) {
            unset_target();
            return drag_event_reply::ignore;
        }

        std::visit(overload{[&](typename Space::wayland_window* win) {
                                // New Wl native target.
                                visit.reset(new wl_visit(win, source));

                                QObject::connect(
                                    visit->qobject.get(),
                                    &wl_visit_qobject::offers_received,
                                    this->qobject.get(),
                                    [this](auto const& offers) { set_offers(offers); });
                            },
                            [&](typename Space::x11_window* win) {
                                // Target is an Xwayland window. Handled here and by X directly.
                                if (win->control) {
                                    if (source.core.space->stacking.active != target) {
                                        win::activate_window(*source.core.space, *win);
                                    }
                                }
                                unset_target();
                            },
                            [&](typename Space::internal_window_t* /*win*/) { unset_target(); }},
                   *target);

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
    std::unique_ptr<wl_visit<Space>> visit;

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
        win::activate_window(*source.core.space, *ac);
        source.core.space->base.server->seat()->drags().set_target(ac->surface,
                                                                   win::get_input_transform(*ac));
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

    x11_source<data_source_ext, Space>& source;
    mime_atoms offers;
    std::vector<std::pair<xcb_timestamp_t, bool>> data_requests;

    std::vector<std::unique_ptr<wl_visit<Space>>> old_visits;
};

}
