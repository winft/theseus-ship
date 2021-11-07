/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_spy.h"

#include <QObject>

class QTimer;

namespace KWin::input
{
class keyboard;
class xkb;

class KWIN_EXPORT keyboard_repeat_spy : public QObject, public input::event_spy
{
    Q_OBJECT
public:
    explicit keyboard_repeat_spy(xkb* xkb);
    ~keyboard_repeat_spy() override;

    void key(key_event const& event) override;

Q_SIGNALS:
    void key_repeated(key_event const& event);

private:
    void handleKeyRepeat();
    QTimer* m_timer;
    xkb* m_xkb;
    quint32 m_time;
    quint32 m_key = 0;
    input::keyboard* keyboard{nullptr};
};

}
