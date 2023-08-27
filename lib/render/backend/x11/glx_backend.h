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
#include "swap_event_filter.h"

#include <epoxy/glx.h>
#include <memory>
#include <unordered_map>
#include <xcb/glx.h>

namespace KWin::render::backend::x11
{

/// OpenGL Backend using GLX over an X overlay window.
template<typename Platform>
class glx_backend : public gl::backend<gl::scene<typename Platform::abstract_type>,
                                       typename Platform::abstract_type>
{
public:
    using type = glx_backend<Platform>;
    using gl_scene = gl::scene<typename Platform::abstract_type>;
    using abstract_type = gl::backend<gl_scene, typename Platform::abstract_type>;
    using x11_compositor_t = typename Platform::compositor_t;

    glx_backend(Display* display, Platform& platform)
        : abstract_type(platform)
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
        base::x11::xcb::sync(platform.base.x11_data.connection);

        // The back buffer contents are now undefined
        m_bufferAge = 0;
    }

    std::unique_ptr<typename abstract_type::texture_priv_t>
    createBackendTexture(typename abstract_type::texture_t* texture) override
    {
        return std::make_unique<GlxTexture<type>>(texture, this);
    }

    effect::render_data set_render_target_to_output(base::output const& output) override
    {
        auto const out_geo = output.geometry();
        auto const viewport = QRect(
            {out_geo.x(), platform.base.topology.size.height() - out_geo.y() - out_geo.height()},
            out_geo.size());

        makeCurrent();
        native_fbo = GLFramebuffer(0, platform.base.topology.size, viewport);
        GLFramebuffer::pushRenderTarget(&native_fbo);

        QMatrix4x4 view;
        QMatrix4x4 projection;
        gl::create_view_projection(output.geometry(), view, projection);
        projection.scale(1, -1);

        effect::render_data data{
            .view = view,
            .projection = projection,
            .viewport = viewport,
            .flip_y = false,
        };

        return data;
    }

    QRegion get_output_render_region(base::output const& output) const override
    {
        if (!this->supportsBufferAge()) {
            return output.geometry();
        }

        return this->accumulatedDamageHistory(m_bufferAge) & output.geometry();
    }

    void endRenderingFrameForScreen(base::output* /*output*/,
                                    QRegion const& renderedRegion,
                                    QRegion const& damagedRegion) override
    {
        GLFramebuffer::popRenderTarget();

        output_render_count++;
        accum_render |= renderedRegion;
        accum_damage |= damagedRegion;

        if (output_render_count != platform.base.outputs.size()) {
            return;
        }

        output_render_count = 0;

        if (GLPlatform::instance()->driver() == Driver_NVidia && !GLPlatform::instance()->isGLES()
            && !this->supportsBufferAge()) {
            if (auto const space = QRegion(QRect({}, platform.base.topology.size));
                accum_render != space) {
                // Copy dirty parts from front to backbuffer.
                glReadBuffer(GL_FRONT);
                this->copyPixels(space - accum_render);
                glReadBuffer(GL_BACK);
                this->setLastDamage(space);
            }
        }

        if (accum_damage.isEmpty()) {
            this->setLastDamage({});

            // If the damaged region of a window is fully occluded, the only
            // rendering done, if any, will have been to repair a reused back
            // buffer, making it identical to the front buffer.
            //
            // In this case we won't post the back buffer. Instead we'll just
            // set the buffer age to 1, so the repaired regions won't be
            // rendered again in the next frame.
            if (!accum_render.isEmpty()) {
                accum_render = {};
                glFlush();
            }

            m_bufferAge = 1;
            return;
        }

        this->setLastDamage(accum_render);

        // Show the window only after the first pass, since that pass may take long.
        if (overlay_window->window()) {
            overlay_window->show();
        }

        if (this->supportsBufferAge()) {
            this->addToDamageHistory(accum_damage);
        }
        accum_damage = {};
        accum_render = {};
    }

    void try_present() override
    {
        present_buffer();
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
    std::unique_ptr<typename x11_compositor_t::overlay_window_t> overlay_window;
    std::unique_ptr<swap_event_filter<x11_compositor_t>> swap_filter;
    std::unordered_map<xcb_visualid_t, fb_config_info*> fb_configs;
    std::unordered_map<xcb_visualid_t, int> visual_depth_hash;

    Platform& platform;

private:
    void present_buffer()
    {
        if (this->lastDamage().isEmpty()) {
            return;
        }

        auto const& space_size = platform.base.topology.size;
        QRegion const displayRegion(0, 0, space_size.width(), space_size.height());
        const bool canSwapBuffers
            = this->supportsBufferAge() || (this->lastDamage() == displayRegion);

        m_needsCompositeTimerStart = true;

        if (canSwapBuffers) {
            if (supportsSwapEvents()) {
                m_needsCompositeTimerStart = false;
                platform.compositor->aboutToSwapBuffers();
            }

            glXSwapBuffers(data.display, data.window);

            if (this->supportsBufferAge()) {
                glXQueryDrawable(data.display,
                                 data.window,
                                 GLX_BACK_BUFFER_AGE_EXT,
                                 reinterpret_cast<GLuint*>(&m_bufferAge));
            }
        } else if (data.extensions.mesa_copy_sub_buffer) {
            for (const QRect& r : this->lastDamage()) {
                // convert to OpenGL coordinates
                int y = space_size.height() - r.y() - r.height();
                glXCopySubBufferMESA(data.display, data.window, r.x(), y, r.width(), r.height());
            }
        } else {
            // Copy Pixels (horribly slow on Mesa).
            glDrawBuffer(GL_FRONT);
            this->copyPixels(this->lastDamage());
            glDrawBuffer(GL_BACK);
        }

        this->setLastDamage(QRegion());
        if (!this->supportsBufferAge()) {
            glXWaitGL();
            XFlush(data.display);
        }
    }

    bool supportsSwapEvents() const
    {
        return static_cast<bool>(swap_filter);
    }

    GLFramebuffer native_fbo;
    int m_bufferAge{0};
    bool m_needsCompositeTimerStart = false;
    size_t output_render_count{0};
    QRegion accum_render;
    QRegion accum_damage;
};

}
