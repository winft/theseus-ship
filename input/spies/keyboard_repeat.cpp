/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_repeat.h"

#include "input/event.h"
#include "input/xkb.h"
#include "wayland_server.h"

#include <Wrapland/Server/seat.h>

#include <QTimer>

namespace KWin::input
{

keyboard_repeat_spy::keyboard_repeat_spy(xkb* xkb)
    : QObject()
    , m_timer(new QTimer(this))
    , m_xkb(xkb)
{
    connect(m_timer, &QTimer::timeout, this, &keyboard_repeat_spy::handleKeyRepeat);
}

keyboard_repeat_spy::~keyboard_repeat_spy() = default;

void keyboard_repeat_spy::handleKeyRepeat()
{
    // TODO: don't depend on WaylandServer
    if (waylandServer()->seat()->keyRepeatRate() != 0) {
        m_timer->setInterval(1000 / waylandServer()->seat()->keyRepeatRate());
    }
    // TODO: better time
    emit keyRepeat(m_key, m_time);
}

void keyboard_repeat_spy::key(key_event const& event)
{
    switch (event.state) {
    case button_state::pressed:
        // TODO: don't get these values from WaylandServer
        if (m_xkb->shouldKeyRepeat(event.keycode)
            && waylandServer()->seat()->keyRepeatDelay() != 0) {
            m_timer->setInterval(waylandServer()->seat()->keyRepeatDelay());
            m_key = event.keycode;
            m_time = event.base.time_msec;
            m_timer->start();
        }
        break;
    case button_state::released:
        if (event.keycode == m_key) {
            m_timer->stop();
        }
        break;
    }
}

}
