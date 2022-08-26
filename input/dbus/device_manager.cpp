/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "device_manager.h"

#include "device.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <algorithm>

namespace KWin::input::dbus
{

device_manager_qobject::device_manager_qobject()
{
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin/InputDevice"),
                                                 QStringLiteral("org.kde.KWin.InputDeviceManager"),
                                                 this,
                                                 QDBusConnection::ExportAllProperties
                                                     | QDBusConnection::ExportAllSignals);
}

device_manager_qobject::~device_manager_qobject()
{
    QDBusConnection::sessionBus().unregisterObject(
        QStringLiteral("/org/kde/KWin/InputDeviceManager"));
}

QStringList device_manager_qobject::devicesSysNames()
{
    QStringList ret;
    for (auto device : devices) {
        ret << device->sys_name.c_str();
    }
    return ret;
}

}
