/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include <base/x11/data.h>

#include <QDateTime>
#include <QWidget>
#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Space>
class screen_edges_filter : public base::x11::event_filter
{
public:
    explicit screen_edges_filter(Space& space)
        : base::x11::event_filter(
            *space.base.x11_event_filters,
            QVector<int>{XCB_MOTION_NOTIFY, XCB_ENTER_NOTIFY, XCB_CLIENT_MESSAGE})
        , space{space}
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        const uint8_t eventType = event->response_type & ~0x80;
        auto get_timepoint
            = [&](auto time) { return base::x11::xcb_time_to_chrono(space.base.x11_data, time); };
        switch (eventType) {
        case XCB_MOTION_NOTIFY: {
            const auto mouseEvent = reinterpret_cast<xcb_motion_notify_event_t*>(event);
            const QPoint rootPos(mouseEvent->root_x, mouseEvent->root_y);
            if (QWidget::mouseGrabber()) {
                space.edges->check(rootPos, get_timepoint(space.base.x11_data.time), true);
            } else {
                space.edges->check(rootPos, get_timepoint(mouseEvent->time));
            }
            // not filtered out
            break;
        }
        case XCB_ENTER_NOTIFY: {
            auto const enter_event = reinterpret_cast<xcb_enter_notify_event_t*>(event);
            auto const window = enter_event->event;
            auto const point = QPoint(enter_event->root_x, enter_event->root_y);

            bool activated = false;
            bool activatedForClient = false;

            for (auto& edge : space.edges->edges) {
                if (!edge || edge->window_id() == XCB_WINDOW_NONE) {
                    continue;
                }
                if (edge->reserved_count == 0 || edge->is_blocked) {
                    continue;
                }
                if (!edge->activatesForPointer()) {
                    continue;
                }

                if (edge->window_id() == window) {
                    if (edge->check(point, get_timepoint(enter_event->time))) {
                        if (edge->client()) {
                            activatedForClient = true;
                        }
                    }
                    activated = true;
                    break;
                }

                if (edge->approachWindow() == window) {
                    edge->startApproaching();
                    // TODO: if it's a corner, it should also trigger for other windows
                    return true;
                }
            }

            if (activatedForClient) {
                for (auto& edge : space.edges->edges) {
                    if (edge->client()) {
                        edge->markAsTriggered(point, get_timepoint(enter_event->time));
                    }
                }
            }

            return activated;
        }
        case XCB_CLIENT_MESSAGE: {
            const auto ce = reinterpret_cast<xcb_client_message_event_t*>(event);
            if (ce->type != space.atoms->xdnd_position) {
                return false;
            }

            auto const point = QPoint(ce->data.data32[2] >> 16, ce->data.data32[2] & 0xffff);

            for (auto& edge : space.edges->edges) {
                if (!edge || edge->window_id() == XCB_WINDOW_NONE) {
                    continue;
                }
                if (edge->reserved_count > 0 && edge->window_id() == ce->window) {
                    base::x11::update_time_from_clock(space.base);
                    edge->check(point, get_timepoint(space.base.x11_data.time), true);
                    return true;
                }
            }
            return false;
        }
        }
        return false;
    }

    Space& space;
};

}
