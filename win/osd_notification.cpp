/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "osd_notification.h"

namespace KWin::win
{

osd_notification_qobject::osd_notification_qobject(QTimer& timer)
    : timer{timer}
{
}

bool osd_notification_qobject::isVisible() const
{
    return m_visible;
}

void osd_notification_qobject::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }

    m_visible = visible;
    Q_EMIT visibleChanged();
}

QString osd_notification_qobject::message() const
{
    return m_message;
}

void osd_notification_qobject::setMessage(const QString& message)
{
    if (m_message == message) {
        return;
    }

    m_message = message;
    Q_EMIT messageChanged();
}

QString osd_notification_qobject::iconName() const
{
    return m_iconName;
}

void osd_notification_qobject::setIconName(const QString& iconName)
{
    if (m_iconName == iconName) {
        return;
    }

    m_iconName = iconName;
    Q_EMIT iconNameChanged();
}

int osd_notification_qobject::timeout() const
{
    return timer.interval();
}

void osd_notification_qobject::setTimeout(int timeout)
{
    if (timer.interval() == timeout) {
        return;
    }

    timer.setInterval(timeout);
    Q_EMIT timeoutChanged();
}

}
