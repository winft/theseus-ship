/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event.h"

namespace KWin::input
{

KeyEvent::KeyEvent(QEvent::Type type,
                   Qt::Key key,
                   Qt::KeyboardModifiers modifiers,
                   quint32 code,
                   quint32 keysym,
                   const QString& text,
                   bool autorepeat,
                   quint32 timestamp,
                   keyboard* device)
    : QKeyEvent(type, key, modifiers, code, keysym, 0, text, autorepeat)
    , m_device(device)
{
    setTimestamp(timestamp);
}

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
