/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositing.h"

#include "compositingadaptor.h"

namespace KWin::render::dbus
{

compositing_qobject::compositing_qobject()
{
    new CompositingAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/Compositor"), this);
    dbus.connect(QString(),
                 QStringLiteral("/Compositor"),
                 QStringLiteral("org.kde.kwin.Compositing"),
                 QStringLiteral("reinit"),
                 this,
                 SLOT(reinitialize()));
}

QString compositing_qobject::compositingNotPossibleReason() const
{
    return integration.not_possible_reason();
}

QString compositing_qobject::compositingType() const
{
    return integration.type();
}

bool compositing_qobject::isActive() const
{
    return integration.active();
}

bool compositing_qobject::isCompositingPossible() const
{
    return integration.possible();
}

bool compositing_qobject::isOpenGLBroken() const
{
    return integration.opengl_broken();
}

bool compositing_qobject::platformRequiresCompositing() const
{
    return integration.required();
}

void compositing_qobject::resume()
{
    if (integration.resume) {
        integration.resume();
    }
}

void compositing_qobject::suspend()
{
    if (integration.suspend) {
        integration.suspend();
    }
}

void compositing_qobject::reinitialize()
{
    return integration.reinit();
}

QStringList compositing_qobject::supportedOpenGLPlatformInterfaces() const
{
    return integration.get_types();
}

}
