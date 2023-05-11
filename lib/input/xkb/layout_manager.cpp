/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "layout_manager.h"

namespace KWin::input::xkb
{

layout_manager_qobject::layout_manager_qobject(std::function<void()> reconfigure_callback)
    : reconfigure_callback{reconfigure_callback}
{
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/Layouts"),
                                          QStringLiteral("org.kde.keyboard"),
                                          QStringLiteral("reloadConfig"),
                                          this,
                                          SLOT(reconfigure()));
}

void layout_manager_qobject::reconfigure()
{
    reconfigure_callback();
}

}
