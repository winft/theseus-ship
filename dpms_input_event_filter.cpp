/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
Copyright 2020 Roman Gilg <subdiff@gmail.com>

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
#include "dpms_input_event_filter.h"

#include "platform.h"
#include "wayland_server.h"

#include <QApplication>

#include <Wrapland/Server/seat.h>

namespace KWin
{

DpmsInputEventFilter::DpmsInputEventFilter(Platform *backend)
    : InputEventFilter()
    , m_backend(backend)
{
}

bool DpmsInputEventFilter::pointerEvent(QMouseEvent *event, uint32_t nativeButton)
{
    Q_UNUSED(event)
    Q_UNUSED(nativeButton)

    notify();
    return true;
}

bool DpmsInputEventFilter::wheelEvent(QWheelEvent *event)
{
    Q_UNUSED(event)

    notify();
    return true;
}

bool DpmsInputEventFilter::keyEvent(QKeyEvent *event)
{
    Q_UNUSED(event)

    notify();
    return true;
}

bool DpmsInputEventFilter::touchDown(int32_t id, const QPointF &pos, uint32_t time)
{
    Q_UNUSED(pos)
    Q_UNUSED(time)
    if (m_touchPoints.isEmpty()) {
        if (!m_doubleTapTimer.isValid()) {
            // This is the first tap.
            m_doubleTapTimer.start();
        } else {
            if (m_doubleTapTimer.elapsed() < qApp->doubleClickInterval()) {
                m_secondTap = true;
            } else {
                // Took too long. Let's consider it a new click.
                m_doubleTapTimer.restart();
            }
        }
    } else {
        // Not a double tap.
        m_doubleTapTimer.invalidate();
        m_secondTap = false;
    }
    m_touchPoints << id;
    return true;
}

bool DpmsInputEventFilter::touchUp(int32_t id, uint32_t time)
{
    m_touchPoints.removeAll(id);
    if (m_touchPoints.isEmpty() && m_doubleTapTimer.isValid() && m_secondTap) {
        if (m_doubleTapTimer.elapsed() < qApp->doubleClickInterval()) {
            waylandServer()->seat()->setTimestamp(time);
            notify();
        }
        m_doubleTapTimer.invalidate();
        m_secondTap = false;
    }
    return true;
}

bool DpmsInputEventFilter::touchMotion(int32_t id, const QPointF &pos, uint32_t time)
{
    Q_UNUSED(id)
    Q_UNUSED(pos)
    Q_UNUSED(time)

    // Ignore the event.
    return true;
}

void DpmsInputEventFilter::notify()
{
    // Queued to not modify the list of event filters while filtering.
    QMetaObject::invokeMethod(m_backend, "turnOutputsOn", Qt::QueuedConnection);
}

}
