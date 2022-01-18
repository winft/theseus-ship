/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositing.h"

#include "compositingadaptor.h"

#include "base/platform.h"
#include "render/platform.h"
#include "render/scene.h"
#include "render/x11/compositor.h"
#include "workspace.h"

#include <QOpenGLContext>

namespace KWin::render::dbus
{

compositing::compositing(render::compositor* parent)
    : QObject(parent)
    , m_compositor(parent)
{
    connect(m_compositor,
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
    return kwinApp()->get_base().render->compositingNotPossibleReason();
}

QString compositing::compositingType() const
{
    if (!m_compositor->scene()) {
        return QStringLiteral("none");
    }
    switch (m_compositor->scene()->compositingType()) {
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
    return m_compositor->isActive();
}

bool compositing::isCompositingPossible() const
{
    return kwinApp()->get_base().render->compositingPossible();
}

bool compositing::isOpenGLBroken() const
{
    return kwinApp()->get_base().render->openGLCompositingIsBroken();
}

bool compositing::platformRequiresCompositing() const
{
    return kwinApp()->get_base().render->requiresCompositing();
}

void compositing::resume()
{
    using X11Compositor = render::x11::compositor;

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        static_cast<X11Compositor*>(m_compositor)->resume(X11Compositor::ScriptSuspend);
    }
}

void compositing::suspend()
{
    using X11Compositor = render::x11::compositor;

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        static_cast<X11Compositor*>(m_compositor)->suspend(X11Compositor::ScriptSuspend);
    }
}

void compositing::reinitialize()
{
    m_compositor->reinitialize();
}

QStringList compositing::supportedOpenGLPlatformInterfaces() const
{
    QStringList interfaces;
    bool supportsGlx = false;
#if HAVE_EPOXY_GLX
    supportsGlx = (kwinApp()->operationMode() == Application::OperationModeX11);
#endif
    if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
        supportsGlx = false;
    }
    if (supportsGlx) {
        interfaces << QStringLiteral("glx");
    }
    interfaces << QStringLiteral("egl");
    return interfaces;
}

}
