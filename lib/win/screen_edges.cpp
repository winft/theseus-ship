/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_edges.h"

// DBus generated
#include "screenlocker_interface.h"

namespace KWin::win
{

void lock_screen_saver_via_dbus()
{
    OrgFreedesktopScreenSaverInterface interface(QStringLiteral("org.freedesktop.ScreenSaver"),
                                                 QStringLiteral("/ScreenSaver"),
                                                 QDBusConnection::sessionBus());
    if (interface.isValid()) {
        interface.Lock();
    }
}

}
