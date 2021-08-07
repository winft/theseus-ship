/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_edge.h"

#include "input/gestures.h"
#include "main.h"
#include "screenedge.h"
#include "wayland_server.h"

#include <Wrapland/Server/seat.h>

#include <QKeyEvent>

namespace KWin::input
{

bool screen_edge_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    Q_UNUSED(nativeButton)
    ScreenEdges::self()->isEntered(event);
    // always forward
    return false;
}
bool screen_edge_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(time)
    // TODO: better check whether a touch sequence is in progress
    if (m_touchInProgress || waylandServer()->seat()->isTouchSequence()) {
        // cancel existing touch
        ScreenEdges::self()->gestureRecognizer()->cancelSwipeGesture();
        m_touchInProgress = false;
        m_id = 0;
        return false;
    }
    if (ScreenEdges::self()->gestureRecognizer()->startSwipeGesture(pos) > 0) {
        m_touchInProgress = true;
        m_id = id;
        m_lastPos = pos;
        return true;
    }
    return false;
}

bool screen_edge_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(time)
    if (m_touchInProgress && m_id == id) {
        ScreenEdges::self()->gestureRecognizer()->updateSwipeGesture(
            QSizeF(pos.x() - m_lastPos.x(), pos.y() - m_lastPos.y()));
        m_lastPos = pos;
        return true;
    }
    return false;
}

bool screen_edge_filter::touchUp(qint32 id, quint32 time)
{
    Q_UNUSED(time)
    if (m_touchInProgress && m_id == id) {
        ScreenEdges::self()->gestureRecognizer()->endSwipeGesture();
        m_touchInProgress = false;
        return true;
    }
    return false;
}

}
