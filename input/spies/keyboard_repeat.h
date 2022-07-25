/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_spy.h"

#include <QObject>
#include <memory>

class QTimer;

namespace KWin::input
{

class keyboard;

class keyboard_repeat_spy_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void key_repeated(key_event const& event);
};

class KWIN_EXPORT keyboard_repeat_spy : public input::event_spy
{
public:
    keyboard_repeat_spy();
    ~keyboard_repeat_spy() override;

    void key(key_event const& event) override;

    std::unique_ptr<keyboard_repeat_spy_qobject> qobject;

private:
    void handleKeyRepeat();

    std::unique_ptr<QTimer> m_timer;
    quint32 m_time;
    quint32 m_key = 0;
    input::keyboard* keyboard{nullptr};
};

}
