/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "platform.h"

#include "base/output.h"
#include <config-kwin.h>
#include "render/compositor.h"
#include "effects.h"
#include <KCoreAddons>
#include "overlaywindow.h"
#include "outline.h"
#include "scene.h"
#include "screens.h"
#include "screenedge.h"
#include "colorcorrection/manager.h"

#include <QX11Info>

#include <cerrno>

namespace KWin
{

Platform::Platform()
    : m_eglDisplay(EGL_NO_DISPLAY)
{
    m_colorCorrect = new ColorCorrect::Manager(this);

    Screens::create(this);
}

Platform::~Platform()
{
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(m_eglDisplay);
    }
}

render::gl::backend *Platform::createOpenGLBackend()
{
    return nullptr;
}

render::qpainter::backend *Platform::createQPainterBackend()
{
    return nullptr;
}

Edge *Platform::createScreenEdge(ScreenEdges *edges)
{
    return new Edge(edges);
}

void Platform::repaint(const QRect &rect)
{
    if (!render::compositor::self()) {
        return;
    }
    render::compositor::self()->addRepaint(rect);
}

void Platform::warpPointer(const QPointF &globalPos)
{
    Q_UNUSED(globalPos)
}

bool Platform::supportsQpaContext() const
{
    auto compositor = render::compositor::self();
    if (Q_UNLIKELY(!compositor)) {
        return false;
    }
    if (Scene *scene = compositor->scene()) {
        return scene->supportsSurfacelessContext();
    }
    return false;
}

EGLDisplay KWin::Platform::sceneEglDisplay() const
{
    return m_eglDisplay;
}

void Platform::setSceneEglDisplay(EGLDisplay display)
{
    m_eglDisplay = display;
}

QSize Platform::screenSize() const
{
    return Screens::self()->size();
}

QVector<QRect> Platform::screenGeometries() const
{
    return QVector<QRect>({QRect(QPoint(0, 0), screenSize())});
}

QVector<qreal> Platform::screenScales() const
{
    return QVector<qreal>({1});
}

bool Platform::requiresCompositing() const
{
    return true;
}

bool Platform::compositingPossible() const
{
    return true;
}

QString Platform::compositingNotPossibleReason() const
{
    return QString();
}

bool Platform::openGLCompositingIsBroken() const
{
    return false;
}

void Platform::createOpenGLSafePoint(OpenGLSafePoint safePoint)
{
    Q_UNUSED(safePoint)
}

void Platform::setupActionForGlobalAccel(QAction *action)
{
    Q_UNUSED(action)
}

OverlayWindow *Platform::createOverlayWindow()
{
    return nullptr;
}

static quint32 monotonicTime()
{
    timespec ts;

    const int result = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (result)
        qCWarning(KWIN_CORE, "Failed to query monotonic time: %s", strerror(errno));

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000L;
}

void Platform::updateXTime()
{
    switch (kwinApp()->operationMode()) {
    case Application::OperationModeX11:
        kwinApp()->setX11Time(QX11Info::getTimestamp(), Application::TimestampUpdate::Always);
        break;

    case Application::OperationModeXwayland:
        kwinApp()->setX11Time(monotonicTime(), Application::TimestampUpdate::Always);
        break;

    default:
        // Do not update the current X11 time stamp if it's the Wayland only session.
        break;
    }
}

OutlineVisual *Platform::createOutline(Outline *outline)
{
    if (render::compositor::compositing()) {
       return new CompositedOutlineVisual(outline);
    }
    return nullptr;
}

Decoration::Renderer *Platform::createDecorationRenderer(Decoration::DecoratedClientImpl *client)
{
    if (render::compositor::self()->scene()) {
        return render::compositor::self()->scene()->createDecorationRenderer(client);
    }
    return nullptr;
}

void Platform::invertScreen()
{
    if (effects) {
        if (Effect *inverter = static_cast<EffectsHandlerImpl*>(effects)->provides(Effect::ScreenInversion)) {
            qCDebug(KWIN_CORE) << "inverting screen using Effect plugin";
            QMetaObject::invokeMethod(inverter, "toggleScreenInversion", Qt::DirectConnection);
        }
    }
}

void Platform::createEffectsHandler(render::compositor *compositor, Scene *scene)
{
    new EffectsHandlerImpl(compositor, scene);
}

QString Platform::supportInformation() const
{
    return QStringLiteral("Name: %1\n").arg(metaObject()->className());
}

clockid_t Platform::clockId() const
{
    return CLOCK_MONOTONIC;
}

}
