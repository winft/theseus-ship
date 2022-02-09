/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_edge.h"

#include "base/wayland/server.h"
#include "input/gestures.h"
#include "input/qt_event.h"
#include "main.h"
#include "win/screen_edges.h"
#include "workspace.h"

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

bool screen_edge_filter::motion(motion_event const& event)
{
    auto qt_event = motion_to_qt_event(event);
    workspace()->edges->isEntered(&qt_event);

    // always forward
    return false;
}

bool screen_edge_filter::touch_down(touch_down_event const& event)
{
    // TODO: better check whether a touch sequence is in progress
    if (m_touchInProgress || waylandServer()->seat()->touches().is_in_progress()) {
        // cancel existing touch
        workspace()->edges->gesture_recognizer->cancelSwipeGesture();
        m_touchInProgress = false;
        m_id = 0;
        return false;
    }
    if (workspace()->edges->gesture_recognizer->startSwipeGesture(event.pos) > 0) {
        m_touchInProgress = true;
        m_id = event.id;
        m_lastPos = event.pos;
        return true;
    }
    return false;
}

bool screen_edge_filter::touch_motion(touch_motion_event const& event)
{
    if (m_touchInProgress && m_id == event.id) {
        workspace()->edges->gesture_recognizer->updateSwipeGesture(
            QSizeF(event.pos.x() - m_lastPos.x(), event.pos.y() - m_lastPos.y()));
        m_lastPos = event.pos;
        return true;
    }
    return false;
}

bool screen_edge_filter::touch_up(touch_up_event const& event)
{
    if (m_touchInProgress && m_id == event.id) {
        workspace()->edges->gesture_recognizer->endSwipeGesture();
        m_touchInProgress = false;
        return true;
    }
    return false;
}

}
