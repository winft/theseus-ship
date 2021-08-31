/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event.h"

namespace KWin::input
{

SwitchEvent::SwitchEvent(State state,
                         quint32 timestamp,
                         quint64 timestampMicroseconds,
                         switch_device* device)
    : QInputEvent(QEvent::User)
    , m_state(state)
    , m_timestampMicroseconds(timestampMicroseconds)
    , m_device(device)
{
    setTimestamp(timestamp);
}

}
