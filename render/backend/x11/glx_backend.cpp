/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    Based on glcompmgr code by Felix Bellaby.
    Using code from Compiz and Beryl.

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "glx_backend.h"

#include "glx.h"
#include "glx_texture.h"
#include "x11_logging.h"

#include "base/platform.h"
#include "base/x11/xcb/helpers.h"
#include "render/gl/texture.h"
#include "render/platform.h"
#include "render/x11/overlay_window.h"
#include "win/x11/geo.h"

#include <kwingl/utils.h>
#include <kwinxrender/utils.h>

#include <QOpenGLContext>
#include <algorithm>
#include <cassert>
#include <deque>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <unistd.h>

namespace KWin::render::backend::x11
{

glx_backend::glx_backend(Display* display,
                         render::x11::compositor<render::x11::platform>& compositor)
    : gl::backend()
    , compositor{compositor}
{
    start_glx_backend(display, compositor, *this);
}

glx_backend::~glx_backend()
{
    if (!data.display) {
        // Already cleaned up.
        return;
    }
    tear_down_glx_backend(*this);
}

int glx_backend::visualDepth(xcb_visualid_t visual) const
{
    auto it = visual_depth_hash.find(visual);
    return it == visual_depth_hash.end() ? 0 : it->second;
}

void glx_backend::present()
{
    if (lastDamage().isEmpty()) {
        return;
    }

    auto const& space_size = compositor.platform.base.topology.size;
    QRegion const displayRegion(0, 0, space_size.width(), space_size.height());
    const bool canSwapBuffers = supportsBufferAge() || (lastDamage() == displayRegion);

    m_needsCompositeTimerStart = true;

    if (canSwapBuffers) {
        if (supportsSwapEvents()) {
            m_needsCompositeTimerStart = false;
            compositor.aboutToSwapBuffers();
        }

        glXSwapBuffers(data.display, data.window);

        if (supportsBufferAge()) {
            glXQueryDrawable(data.display,
                             data.window,
                             GLX_BACK_BUFFER_AGE_EXT,
                             reinterpret_cast<GLuint*>(&m_bufferAge));
        }
    } else if (data.extensions.mesa_copy_sub_buffer) {
        for (const QRect& r : lastDamage()) {
            // convert to OpenGL coordinates
            int y = space_size.height() - r.y() - r.height();
            glXCopySubBufferMESA(data.display, data.window, r.x(), y, r.width(), r.height());
        }
    } else {
        // Copy Pixels (horribly slow on Mesa).
        glDrawBuffer(GL_FRONT);
        copyPixels(lastDamage());
        glDrawBuffer(GL_BACK);
    }

    setLastDamage(QRegion());
    if (!supportsBufferAge()) {
        glXWaitGL();
        XFlush(data.display);
    }
}

void glx_backend::screenGeometryChanged(const QSize& size)
{
    overlay_window->resize(size);
    doneCurrent();

    XMoveResizeWindow(data.display, window, 0, 0, size.width(), size.height());
    overlay_window->setup(window);
    base::x11::xcb::sync();

    makeCurrent();
    glViewport(0, 0, size.width(), size.height());

    // The back buffer contents are now undefined
    m_bufferAge = 0;
}

gl::texture_private<gl::backend>*
glx_backend::createBackendTexture(gl::texture<gl::backend>* texture)
{
    return new GlxTexture<glx_backend>(texture, this);
}

QRegion glx_backend::prepareRenderingFrame()
{
    QRegion repaint;

    if (supportsBufferAge())
        repaint = accumulatedDamageHistory(m_bufferAge);

    startRenderTimer();

    native_fbo = GLRenderTarget(0, QRect({}, compositor.platform.base.topology.size));
    GLRenderTarget::pushRenderTarget(&native_fbo);

    return repaint;
}

void glx_backend::endRenderingFrame(const QRegion& renderedRegion, const QRegion& damagedRegion)
{
    GLRenderTarget::popRenderTarget();

    if (damagedRegion.isEmpty()) {
        setLastDamage(QRegion());

        // If the damaged region of a window is fully occluded, the only
        // rendering done, if any, will have been to repair a reused back
        // buffer, making it identical to the front buffer.
        //
        // In this case we won't post the back buffer. Instead we'll just
        // set the buffer age to 1, so the repaired regions won't be
        // rendered again in the next frame.
        if (!renderedRegion.isEmpty())
            glFlush();

        m_bufferAge = 1;
        return;
    }

    setLastDamage(renderedRegion);
    present();

    // Show the window only after the first pass, since that pass may take long.
    if (overlay_window->window()) {
        overlay_window->show();
    }

    // Save the damaged region to history
    if (supportsBufferAge())
        addToDamageHistory(damagedRegion);
}

bool glx_backend::makeCurrent()
{
    if (QOpenGLContext* context = QOpenGLContext::currentContext()) {
        // Workaround to tell Qt that no QOpenGLContext is current
        context->doneCurrent();
    }
    const bool current = glXMakeCurrent(data.display, data.window, data.context);
    return current;
}

void glx_backend::doneCurrent()
{
    glXMakeCurrent(data.display, None, nullptr);
}

bool glx_backend::supportsSwapEvents() const
{
    return data.swap_filter != nullptr;
}

bool glx_backend::hasSwapEvent() const
{
    return !m_needsCompositeTimerStart;
}

}
