/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "virtual_desktop_types.h"

// Marshall the virtual_desktop_data into a D-BUS argument
QDBusArgument const& operator<<(QDBusArgument& argument,
                                KWin::win::dbus::virtual_desktop_data const& desk)
{
    argument.beginStructure();
    argument << desk.position;
    argument << desk.id;
    argument << desk.name;
    argument.endStructure();
    return argument;
}

// Retrieve
QDBusArgument const& operator>>(QDBusArgument const& argument,
                                KWin::win::dbus::virtual_desktop_data& desk)
{
    argument.beginStructure();
    argument >> desk.position;
    argument >> desk.id;
    argument >> desk.name;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator<<(QDBusArgument& argument,
                                KWin::win::dbus::virtual_desktop_data_vector const& deskVector)
{
    argument.beginArray(qMetaTypeId<KWin::win::dbus::virtual_desktop_data>());

    for (int i = 0; i < deskVector.size(); ++i) {
        argument << deskVector[i];
    }

    argument.endArray();
    return argument;
}

const QDBusArgument& operator>>(QDBusArgument const& argument,
                                KWin::win::dbus::virtual_desktop_data_vector& deskVector)
{
    argument.beginArray();
    deskVector.clear();

    while (!argument.atEnd()) {
        KWin::win::dbus::virtual_desktop_data element;
        argument >> element;
        deskVector.append(element);
    }

    argument.endArray();

    return argument;
}
