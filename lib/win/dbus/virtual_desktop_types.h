/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QtDBus>
#include <kwin_export.h>

namespace KWin::win::dbus
{

struct subspace_data {
    uint position;
    QString id;
    QString name;
};

using subspace_data_vector = QVector<subspace_data>;

}

KWIN_EXPORT QDBusArgument const& operator<<(QDBusArgument& argument,
                                            KWin::win::dbus::subspace_data const& desk);
KWIN_EXPORT QDBusArgument const& operator>>(QDBusArgument const& argument,
                                            KWin::win::dbus::subspace_data& desk);

Q_DECLARE_METATYPE(KWin::win::dbus::subspace_data)

KWIN_EXPORT QDBusArgument const&
operator<<(QDBusArgument& argument, KWin::win::dbus::subspace_data_vector const& deskVector);
KWIN_EXPORT QDBusArgument const& operator>>(QDBusArgument const& argument,
                                            KWin::win::dbus::subspace_data_vector& deskVector);

Q_DECLARE_METATYPE(KWin::win::dbus::subspace_data_vector)
