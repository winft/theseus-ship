/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_edges_filter.h"

#include "base/x11/atoms.h"
#include "win/screen_edges.h"
#include "win/space.h"

#include <QWidget>
#include <xcb/xcb.h>

namespace KWin::win::x11
{

screen_edges_filter::screen_edges_filter()
    : base::x11::event_filter(QVector<int>{XCB_MOTION_NOTIFY, XCB_ENTER_NOTIFY, XCB_CLIENT_MESSAGE})
{
}

bool screen_edges_filter::event(xcb_generic_event_t* event)
{
    const uint8_t eventType = event->response_type & ~0x80;
    switch (eventType) {
    case XCB_MOTION_NOTIFY: {
        const auto mouseEvent = reinterpret_cast<xcb_motion_notify_event_t*>(event);
        const QPoint rootPos(mouseEvent->root_x, mouseEvent->root_y);
        if (QWidget::mouseGrabber()) {
            workspace()->edges->check(
                rootPos, QDateTime::fromMSecsSinceEpoch(xTime(), Qt::UTC), true);
        } else {
            workspace()->edges->check(rootPos,
                                      QDateTime::fromMSecsSinceEpoch(mouseEvent->time, Qt::UTC));
        }
        // not filtered out
        break;
    }
    case XCB_ENTER_NOTIFY: {
        const auto enter = reinterpret_cast<xcb_enter_notify_event_t*>(event);
        return workspace()->edges->handleEnterNotifiy(
            enter->event,
            QPoint(enter->root_x, enter->root_y),
            QDateTime::fromMSecsSinceEpoch(enter->time, Qt::UTC));
    }
    case XCB_CLIENT_MESSAGE: {
        const auto ce = reinterpret_cast<xcb_client_message_event_t*>(event);
        if (ce->type != workspace()->atoms->xdnd_position) {
            return false;
        }
        return workspace()->edges->handleDndNotify(
            ce->window, QPoint(ce->data.data32[2] >> 16, ce->data.data32[2] & 0xffff));
    }
    }
    return false;
}

}
