/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor.h"
#include "effects.h"
#include "gl/egl_data.h"
#include "post/night_color_manager.h"
#include "singleton_interface.h"

#include <memory>

namespace KWin
{

namespace base
{
class platform;
}

namespace win::deco
{
template<typename Window>
class client_impl;
template<typename Client>
class renderer;
}

namespace render
{

namespace gl
{
class backend;
}
namespace qpainter
{
class backend;
}

class outline;
class outline_visual;
class scene;

class platform
{
public:
    virtual ~platform()
    {
        singleton_interface::get_egl_data = {};
    }

    virtual render::gl::backend* get_opengl_backend(render::compositor& /*compositor*/)
    {
        return nullptr;
    }

    virtual render::qpainter::backend* get_qpainter_backend(render::compositor& /*compositor*/)
    {
        return nullptr;
    }

    // TODO(romangg): Remove the boolean trap.
    virtual void render_stop(bool on_shutdown) = 0;

    virtual bool requiresCompositing() const = 0;
    virtual bool compositingPossible() const = 0;
    virtual QString compositingNotPossibleReason() const = 0;
    virtual bool openGLCompositingIsBroken() const = 0;
    virtual void createOpenGLSafePoint(OpenGLSafePoint safePoint) = 0;

    virtual render::outline_visual* create_non_composited_outline(render::outline* outline) = 0;
    virtual win::deco::renderer<win::deco::client_impl<Toplevel>>*
    createDecorationRenderer(win::deco::client_impl<Toplevel>* client)
        = 0;
    virtual std::unique_ptr<effects_handler_impl>
    createEffectsHandler(render::compositor* compositor, render::scene* scene) = 0;

    /**
     * Platform specific way to invert the screen.
     * Default implementation invokes the invert effect
     */
    virtual void invertScreen() = 0;

    virtual CompositingType selected_compositor() const = 0;

    std::unique_ptr<render::post::night_color_manager> night_color;
    std::unique_ptr<render::compositor> compositor;
    base::platform& base;

    gl::egl_data* egl_data{nullptr};

protected:
    platform(base::platform& base)
        : night_color{std::make_unique<render::post::night_color_manager>()}
        , base{base}
    {
        singleton_interface::get_egl_data = [this] { return egl_data; };
    }
};

}
}
