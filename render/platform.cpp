/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "compositor.h"
#include "effects.h"
#include "outline.h"
#include "post/night_color_manager.h"
#include "scene.h"
#include "singleton_interface.h"

#include "base/logging.h"
#include "config-kwin.h"
#include "main.h"

namespace KWin::render
{

platform::platform(base::platform& base)
    : night_color{std::make_unique<render::post::night_color_manager>()}
    , base{base}
{
    singleton_interface::platform = this;
}

platform::~platform()
{
    singleton_interface::platform = nullptr;
}

render::gl::backend* platform::get_opengl_backend(render::compositor& /*compositor*/)
{
    return nullptr;
}

render::qpainter::backend* platform::get_qpainter_backend(render::compositor& /*compositor*/)
{
    return nullptr;
}

bool platform::requiresCompositing() const
{
    return true;
}

bool platform::compositingPossible() const
{
    return true;
}

QString platform::compositingNotPossibleReason() const
{
    return QString();
}

bool platform::openGLCompositingIsBroken() const
{
    return false;
}

void platform::createOpenGLSafePoint(OpenGLSafePoint safePoint)
{
    Q_UNUSED(safePoint)
}

render::outline_visual* platform::create_non_composited_outline(render::outline* /*outline*/)
{
    return nullptr;
}

win::deco::renderer* platform::createDecorationRenderer(win::deco::client_impl* client)
{
    if (compositor->scene) {
        return compositor->scene->createDecorationRenderer(client);
    }
    return nullptr;
}

void platform::invertScreen()
{
    if (effects) {
        static_cast<render::effects_handler_impl*>(effects)->invert_screen();
    }
}

}
