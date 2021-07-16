/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

namespace KWin::input::dbus
{

inline void inform_touchpad_toggle(bool enabled)
{
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                              QStringLiteral("/org/kde/osdService"),
                                              QStringLiteral("org.kde.osdService"),
                                              QStringLiteral("touchpadEnabledChanged"));
    msg.setArguments({enabled});
    QDBusConnection::sessionBus().asyncCall(msg);
}

}
