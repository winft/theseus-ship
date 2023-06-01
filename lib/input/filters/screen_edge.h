/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/qt_event.h"
#include "win/screen_edges.h"

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

template<typename Redirect>
class screen_edge_filter : public event_filter<Redirect>
{
public:
    explicit screen_edge_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool motion(motion_event const& event) override
    {
        auto qt_event = motion_to_qt_event(*this->redirect.pointer, event);
        qt_event.setTimestamp(event.base.time_msec);
        this->redirect.space.edges->isEntered(&qt_event);

        // always forward
        return false;
    }

    bool touch_down(touch_down_event const& event) override
    {
        // TODO: better check whether a touch sequence is in progress
        if (m_touchInProgress
            || this->redirect.platform.base.server->seat()->touches().is_in_progress()) {
            // cancel existing touch
            this->redirect.space.edges->gesture_recognizer->cancelSwipeGesture();
            m_touchInProgress = false;
            m_id = 0;
            return false;
        }
        if (this->redirect.space.edges->gesture_recognizer->startSwipeGesture(event.pos) > 0) {
            m_touchInProgress = true;
            m_id = event.id;
            m_lastPos = event.pos;
            return true;
        }
        return false;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        if (m_touchInProgress && m_id == event.id) {
            this->redirect.space.edges->gesture_recognizer->updateSwipeGesture(
                QSizeF(event.pos.x() - m_lastPos.x(), event.pos.y() - m_lastPos.y()));
            m_lastPos = event.pos;
            return true;
        }
        return false;
    }

    bool touch_up(touch_up_event const& event) override
    {
        if (m_touchInProgress && m_id == event.id) {
            this->redirect.space.edges->gesture_recognizer->endSwipeGesture();
            m_touchInProgress = false;
            return true;
        }
        return false;
    }

private:
    bool m_touchInProgress = false;
    qint32 m_id = 0;
    QPointF m_lastPos;
};

}
