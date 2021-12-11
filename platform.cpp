/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "config-kwin.h"
#include "main.h"
#include "render/compositor.h"
#include "render/effects.h"
#include "render/outline.h"
#include "render/post/night_color_manager.h"
#include "render/scene.h"

#include <QX11Info>
#include <cerrno>

namespace KWin
{

Platform::Platform()
    : night_color{std::make_unique<render::post::night_color_manager>()}
{
}

Platform::~Platform()
{
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(m_eglDisplay);
    }
}

render::gl::backend* Platform::createOpenGLBackend(render::compositor* /*compositor*/)
{
    return nullptr;
}

render::qpainter::backend* Platform::createQPainterBackend()
{
    return nullptr;
}

EGLDisplay Platform::sceneEglDisplay() const
{
    return m_eglDisplay;
}

void Platform::setSceneEglDisplay(EGLDisplay display)
{
    m_eglDisplay = display;
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

void Platform::setupActionForGlobalAccel(QAction* action)
{
    Q_UNUSED(action)
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

render::outline_visual* Platform::createOutline(render::outline* outline)
{
    if (render::compositor::compositing()) {
        return new render::composited_outline_visual(outline);
    }
    return nullptr;
}

Decoration::Renderer* Platform::createDecorationRenderer(Decoration::DecoratedClientImpl* client)
{
    if (render::compositor::self()->scene()) {
        return render::compositor::self()->scene()->createDecorationRenderer(client);
    }
    return nullptr;
}

void Platform::invertScreen()
{
    if (effects) {
        if (auto inverter = static_cast<render::effects_handler_impl*>(effects)->provides(
                Effect::ScreenInversion)) {
            qCDebug(KWIN_CORE) << "inverting screen using Effect plugin";
            QMetaObject::invokeMethod(inverter, "toggleScreenInversion", Qt::DirectConnection);
        }
    }
}

void Platform::createEffectsHandler(render::compositor* compositor, render::scene* scene)
{
    new render::effects_handler_impl(compositor, scene);
}

clockid_t Platform::clockId() const
{
    return CLOCK_MONOTONIC;
}

}
