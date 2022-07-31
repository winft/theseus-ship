/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "gl/egl_data.h"

#include "input/redirect.h"
#include "kwin_export.h"
#include "kwinglobals.h"

#include <QObject>
#include <epoxy/egl.h>
#include <memory>

namespace KWin
{

namespace base
{
class platform;
}

namespace win::deco
{
class client_impl;
class renderer;
}

namespace render
{

namespace gl
{
class backend;
}
namespace post
{
class night_color_manager;
}
namespace qpainter
{
class backend;
}

class compositor;
class effects_handler_impl;
class outline;
class outline_visual;
class scene;

class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
public:
    ~platform() override;

    virtual render::gl::backend* get_opengl_backend(render::compositor& compositor);
    virtual render::qpainter::backend* get_qpainter_backend(render::compositor& compositor);

    // TODO(romangg): Remove the boolean trap.
    virtual void render_stop(bool on_shutdown) = 0;

    virtual bool requiresCompositing() const = 0;
    virtual bool compositingPossible() const = 0;
    virtual QString compositingNotPossibleReason() const = 0;
    virtual bool openGLCompositingIsBroken() const = 0;
    virtual void createOpenGLSafePoint(OpenGLSafePoint safePoint) = 0;

    virtual render::outline_visual* create_non_composited_outline(render::outline* outline) = 0;
    virtual win::deco::renderer* createDecorationRenderer(win::deco::client_impl* client) = 0;
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
    platform(base::platform& base);
};

}
}
