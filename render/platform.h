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
class outline;
class outline_visual;
class scene;

class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
public:
    ~platform() override;

    virtual render::gl::backend* createOpenGLBackend(render::compositor& compositor);
    virtual render::qpainter::backend* createQPainterBackend(render::compositor& compositor);

    // TODO(romangg): Remove the boolean trap.
    virtual void render_stop(bool on_shutdown) = 0;

    /**
     * Whether the Platform requires compositing for rendering.
     * Default implementation returns @c true. If the implementing Platform allows to be used
     * without compositing (e.g. rendering is done by the windowing system), re-implement this
     * method.
     */
    virtual bool requiresCompositing() const;
    /**
     * Whether Compositing is possible in the Platform.
     * Returning @c false in this method makes only sense if requiresCompositing returns @c false.
     *
     * The default implementation returns @c true.
     * @see requiresCompositing
     */
    virtual bool compositingPossible() const;
    /**
     * Returns a user facing text explaining why compositing is not possible in case
     * compositingPossible returns @c false.
     *
     * The default implementation returns an empty string.
     * @see compositingPossible
     */
    virtual QString compositingNotPossibleReason() const;
    /**
     * Whether OpenGL compositing is broken.
     * The Platform can implement this method if it is able to detect whether OpenGL compositing
     * broke (e.g. triggered a crash in a previous run).
     *
     * Default implementation returns @c false.
     * @see createOpenGLSafePoint
     */
    virtual bool openGLCompositingIsBroken() const;
    /**
     * This method is invoked before and after creating the OpenGL rendering Scene.
     * An implementing Platform can use it to detect crashes triggered by the OpenGL implementation.
     * This can be used for openGLCompositingIsBroken.
     *
     * The default implementation does nothing.
     * @see openGLCompositingIsBroken.
     */
    virtual void createOpenGLSafePoint(OpenGLSafePoint safePoint);

    /**
     * Creates the outline_visual for the given @p outline.
     * Default implementation creates an outline_visual suited for composited usage.
     */
    virtual render::outline_visual* createOutline(render::outline* outline);

    /**
     * Creates the deco renderer for the given @p client.
     *
     * The default implementation creates a Renderer suited for the Compositor, @c nullptr if there
     * is no Compositor.
     */
    virtual win::deco::renderer* createDecorationRenderer(win::deco::client_impl* client);
    virtual void createEffectsHandler(render::compositor* compositor, render::scene* scene) = 0;

    /**
     * Platform specific way to invert the screen.
     * Default implementation invokes the invert effect
     */
    virtual void invertScreen();

    /**
     * The CompositingTypes supported by the Platform.
     * The first item should be the most preferred one.
     * @since 5.11
     */
    virtual QVector<CompositingType> supportedCompositors() const = 0;

    std::unique_ptr<render::post::night_color_manager> night_color;

    /**
     * The compositor plugin which got selected from @ref supportedCompositors. Prior to selecting
     * this returns @c NoCompositing. Allows to limit the offerings in @ref supportedCompositors
     * in case they do not support runtime compositor switching.
     */
    CompositingType selected_compositor{NoCompositing};

    std::unique_ptr<render::compositor> compositor;
    base::platform& base;

    gl::egl_data* egl_data{nullptr};

protected:
    platform(base::platform& base);
};

}
}
