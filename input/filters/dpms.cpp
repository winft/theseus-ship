/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "dpms.h"

#include "base/wayland/output_helpers.h"
#include "main.h"
#include "platform.h"
#include "wayland_server.h"

#include <QApplication>
#include <QTimer>

#include <Wrapland/Server/seat.h>

namespace KWin::input
{

dpms_filter::dpms_filter(wayland::platform* input)
    : event_filter()
    , input{input}
{
}

bool dpms_filter::button([[maybe_unused]] button_event const& event)
{
    notify();
    return true;
}

bool dpms_filter::motion([[maybe_unused]] motion_event const& event)
{
    notify();
    return true;
}

bool dpms_filter::axis([[maybe_unused]] axis_event const& event)
{
    notify();
    return true;
}

bool dpms_filter::key(key_event const& /*event*/)
{
    notify();
    return true;
}

bool dpms_filter::touchDown(int32_t id, const QPointF& pos, uint32_t time)
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

bool dpms_filter::touchUp(int32_t id, uint32_t time)
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

bool dpms_filter::touchMotion(int32_t id, const QPointF& pos, uint32_t time)
{
    Q_UNUSED(id)
    Q_UNUSED(pos)
    Q_UNUSED(time)

    // Ignore the event.
    return true;
}

void dpms_filter::notify()
{
    // Queued to not modify the list of event filters while filtering.
    QTimer::singleShot(0, input, [this] { input->turn_outputs_on(); });
}

}
