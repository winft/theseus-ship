/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/event_spy.h"
#include "input/keyboard.h"
#include "input/xkb/keyboard.h"
#include "main.h"

#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <QObject>
#include <QTimer>
#include <memory>

namespace KWin::input
{

class KWIN_EXPORT keyboard_repeat_spy_qobject : public QObject
{
    Q_OBJECT
public:
    ~keyboard_repeat_spy_qobject();

Q_SIGNALS:
    void key_repeated(key_event const& event);
};

template<typename Redirect>
class keyboard_repeat_spy : public input::event_spy<Redirect>
{
public:
    keyboard_repeat_spy(Redirect& redirect)
        : event_spy<Redirect>(redirect)
        , qobject{std::make_unique<keyboard_repeat_spy_qobject>()}
        , m_timer{std::make_unique<QTimer>()}
    {
        QObject::connect(
            m_timer.get(), &QTimer::timeout, qobject.get(), [this] { handleKeyRepeat(); });
    }

    ~keyboard_repeat_spy() = default;

    void key(key_event const& event) override
    {
        if (keyboard && keyboard != event.base.dev) {
            return;
        }
        switch (event.state) {
        case key_state::pressed: {
            // TODO: don't get these values from WaylandServer
            auto const delay = waylandServer()->seat()->keyboards().get_repeat_info().delay;
            if (event.base.dev->xkb->should_key_repeat(event.keycode) && delay != 0) {
                m_timer->setInterval(delay);
                m_key = event.keycode;
                keyboard = event.base.dev;
                m_time = event.base.time_msec;
                m_timer->start();
            }
            break;
        }
        case key_state::released:
            if (event.keycode == m_key) {
                m_timer->stop();
                keyboard = nullptr;
            }
            break;
        }
    }

    std::unique_ptr<keyboard_repeat_spy_qobject> qobject;

private:
    void handleKeyRepeat()
    {
        // TODO: don't depend on WaylandServer
        auto const rate = waylandServer()->seat()->keyboards().get_repeat_info().rate;
        if (rate != 0) {
            m_timer->setInterval(1000 / rate);
        }
        // TODO: better time
        Q_EMIT qobject->key_repeated({m_key, key_state::pressed, false, {keyboard, m_time}});
    }

    std::unique_ptr<QTimer> m_timer;
    quint32 m_time;
    quint32 m_key = 0;
    input::keyboard* keyboard{nullptr};
};

}
