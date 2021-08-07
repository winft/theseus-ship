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

void keyboard_repeat_spy::keyEvent(input::KeyEvent* event)
{
    if (event->isAutoRepeat()) {
        return;
    }
    const quint32 key = event->nativeScanCode();
    if (event->type() == QEvent::KeyPress) {
        // TODO: don't get these values from WaylandServer
        if (m_xkb->shouldKeyRepeat(key) && waylandServer()->seat()->keyRepeatDelay() != 0) {
            m_timer->setInterval(waylandServer()->seat()->keyRepeatDelay());
            m_key = key;
            m_time = event->timestamp();
            m_timer->start();
        }
    } else if (event->type() == QEvent::KeyRelease) {
        if (key == m_key) {
            m_timer->stop();
        }
    }
}

}
