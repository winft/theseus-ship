/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

#include "kwinglobals.h"

#include <QDateTime>
#include <QWidget>

namespace KWin::win::x11
{

template<typename Space>
class screen_edges_filter : public base::x11::event_filter
{
public:
    explicit screen_edges_filter(Space& space)
        : base::x11::event_filter(
            QVector<int>{XCB_MOTION_NOTIFY, XCB_ENTER_NOTIFY, XCB_CLIENT_MESSAGE})
        , space{space}
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        const uint8_t eventType = event->response_type & ~0x80;
        switch (eventType) {
        case XCB_MOTION_NOTIFY: {
            const auto mouseEvent = reinterpret_cast<xcb_motion_notify_event_t*>(event);
            const QPoint rootPos(mouseEvent->root_x, mouseEvent->root_y);
            if (QWidget::mouseGrabber()) {
                space.edges->check(rootPos, QDateTime::fromMSecsSinceEpoch(xTime(), Qt::UTC), true);
            } else {
                space.edges->check(rootPos,
                                   QDateTime::fromMSecsSinceEpoch(mouseEvent->time, Qt::UTC));
            }
            // not filtered out
            break;
        }
        case XCB_ENTER_NOTIFY: {
            const auto enter = reinterpret_cast<xcb_enter_notify_event_t*>(event);
            return space.edges->handleEnterNotifiy(
                enter->event,
                QPoint(enter->root_x, enter->root_y),
                QDateTime::fromMSecsSinceEpoch(enter->time, Qt::UTC));
        }
        case XCB_CLIENT_MESSAGE: {
            const auto ce = reinterpret_cast<xcb_client_message_event_t*>(event);
            if (ce->type != space.atoms->xdnd_position) {
                return false;
            }
            return space.edges->handleDndNotify(
                ce->window, QPoint(ce->data.data32[2] >> 16, ce->data.data32[2] & 0xffff));
        }
        }
        return false;
    }

    Space& space;
};

}
