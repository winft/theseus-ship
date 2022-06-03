/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositing.h"

#include "compositingadaptor.h"

#include "base/platform.h"
#include "render/compositor.h"
#include "render/platform.h"
#include "render/scene.h"

#include <QOpenGLContext>

namespace KWin::render::dbus
{

compositing::compositing(render::compositor& compositor)
    : compositor{compositor}
{
    connect(&compositor,
            &render::compositor::compositingToggled,
            this,
            &compositing::compositingToggled);
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

QString compositing::compositingNotPossibleReason() const
{
    return compositor.platform.compositingNotPossibleReason();
}

QString compositing::compositingType() const
{
    if (!compositor.scene) {
        return QStringLiteral("none");
    }

    switch (compositor.scene->compositingType()) {
    case XRenderCompositing:
        return QStringLiteral("xrender");
    case OpenGLCompositing:
        if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
            return QStringLiteral("gles");
        } else {
            return QStringLiteral("gl2");
        }
    case QPainterCompositing:
        return QStringLiteral("qpainter");
    case NoCompositing:
    default:
        return QStringLiteral("none");
    }
}

bool compositing::isActive() const
{
    return compositor.isActive();
}

bool compositing::isCompositingPossible() const
{
    return compositor.platform.compositingPossible();
}

bool compositing::isOpenGLBroken() const
{
    return compositor.platform.openGLCompositingIsBroken();
}

bool compositing::platformRequiresCompositing() const
{
    return compositor.platform.requiresCompositing();
}

void compositing::resume()
{
    if (integration.resume) {
        integration.resume();
    }
}

void compositing::suspend()
{
    if (integration.suspend) {
        integration.suspend();
    }
}

void compositing::reinitialize()
{
    compositor.reinitialize();
}

QStringList compositing::supportedOpenGLPlatformInterfaces() const
{
    return integration.get_types();
}

}
