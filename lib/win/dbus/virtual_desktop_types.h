/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QtDBus>

namespace KWin::win::dbus
{

struct virtual_desktop_data {
    uint position;
    QString id;
    QString name;
};

using virtual_desktop_data_vector = QVector<virtual_desktop_data>;

}

QDBusArgument const& operator<<(QDBusArgument& argument,
                                KWin::win::dbus::virtual_desktop_data const& desk);
QDBusArgument const& operator>>(QDBusArgument const& argument,
                                KWin::win::dbus::virtual_desktop_data& desk);

Q_DECLARE_METATYPE(KWin::win::dbus::virtual_desktop_data)

QDBusArgument const& operator<<(QDBusArgument& argument,
                                KWin::win::dbus::virtual_desktop_data_vector const& deskVector);
QDBusArgument const& operator>>(QDBusArgument const& argument,
                                KWin::win::dbus::virtual_desktop_data_vector& deskVector);

Q_DECLARE_METATYPE(KWin::win::dbus::virtual_desktop_data_vector)
