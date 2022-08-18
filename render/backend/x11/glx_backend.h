/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/options.h"
#include "base/x11/xcb/helpers.h"
#include "render/gl/backend.h"
#include "render/gl/texture.h"
#include "render/x11/overlay_window.h"

// Must be included late because of Qt.
#include "glx.h"
#include "glx_data.h"
#include "glx_fb_config.h"
#include "glx_texture.h"

#include <epoxy/glx.h>
#include <fixx11h.h>
#include <memory>
#include <unordered_map>
#include <xcb/glx.h>

namespace KWin::render::backend::x11
{

/// OpenGL Backend using GLX over an X overlay window.
template<typename Platform>
class glx_backend : public gl::backend
{
public:
    using backend_t = gl::backend;
    using x11_compositor_t = render::x11::compositor<render::x11::platform>;

    glx_backend(Display* display, Platform& platform)
        : gl::backend()
        , platform{platform}
    {
        start_glx_backend(display, static_cast<x11_compositor_t&>(*platform.compositor), *this);
    }

    ~glx_backend() override
    {
        if (!data.display) {
            // Already cleaned up.
            return;
        }
        tear_down_glx_backend(*this);
    }

    void screenGeometryChanged(const QSize& size) override
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
    createBackendTexture(gl::texture<gl::backend>* texture) override
    {
        return new GlxTexture<glx_backend>(texture, this);
    }

    QRegion prepareRenderingFrame() override
    {
        QRegion repaint;

        if (supportsBufferAge())
            repaint = accumulatedDamageHistory(m_bufferAge);

        startRenderTimer();

        native_fbo = GLRenderTarget(0, QRect({}, platform.base.topology.size));
        GLRenderTarget::pushRenderTarget(&native_fbo);

        return repaint;
    }

    void endRenderingFrame(QRegion const& renderedRegion, QRegion const& damagedRegion) override
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

    bool makeCurrent() override
    {
        if (auto context = QOpenGLContext::currentContext()) {
            // Workaround to tell Qt that no QOpenGLContext is current
            context->doneCurrent();
        }
        const bool current = glXMakeCurrent(data.display, data.window, data.context);
        return current;
    }

    void doneCurrent() override
    {
        glXMakeCurrent(data.display, None, nullptr);
    }

    bool hasSwapEvent() const override
    {
        return !m_needsCompositeTimerStart;
    }

    int visualDepth(xcb_visualid_t visual) const
    {
        auto it = visual_depth_hash.find(visual);
        return it == visual_depth_hash.end() ? 0 : it->second;
    }

    glx_data data;

    Window window{None};
    std::unique_ptr<x11_compositor_t::overlay_window_t> overlay_window;
    std::unordered_map<xcb_visualid_t, fb_config_info*> fb_configs;
    std::unordered_map<xcb_visualid_t, int> visual_depth_hash;

    Platform& platform;

protected:
    void present() override
    {
        if (lastDamage().isEmpty()) {
            return;
        }

        auto const& space_size = platform.base.topology.size;
        QRegion const displayRegion(0, 0, space_size.width(), space_size.height());
        const bool canSwapBuffers = supportsBufferAge() || (lastDamage() == displayRegion);

        m_needsCompositeTimerStart = true;

        if (canSwapBuffers) {
            if (supportsSwapEvents()) {
                m_needsCompositeTimerStart = false;
                platform.compositor->aboutToSwapBuffers();
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

private:
    bool supportsSwapEvents() const
    {
        return data.swap_filter != nullptr;
    }

    GLRenderTarget native_fbo;
    int m_bufferAge{0};
    bool m_needsCompositeTimerStart = false;
};

}
