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

#include <cerrno>

namespace KWin
{

Platform::Platform()
    : night_color{std::make_unique<render::post::night_color_manager>()}
{
}

Platform::~Platform()
{
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
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
