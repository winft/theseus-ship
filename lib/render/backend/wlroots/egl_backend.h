/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <config-kwin.h>

#include "egl_helpers.h"
#include "egl_output.h"
#include "egl_texture.h"
#include "wlr_helpers.h"

#include "base/backend/wlroots/output.h"
#include "render/gl/backend.h"
#include "render/gl/egl.h"
#include "render/gl/gl.h"
#include "render/wayland/egl.h"
#include "render/wayland/egl_data.h"

#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <QOpenGLContext>
#include <Wrapland/Server/linux_dmabuf_v1.h>
#include <memory>
#include <stdexcept>

namespace KWin::render::backend::wlroots
{

template<typename Platform>
class egl_backend : public gl::backend<gl::scene<typename Platform::abstract_type>,
                                       typename Platform::abstract_type>
{
public:
    using type = egl_backend<Platform>;
    using gl_scene = gl::scene<typename Platform::abstract_type>;
    using abstract_type = gl::backend<gl_scene, typename Platform::abstract_type>;
    using egl_output_t = egl_output<typename Platform::output_t>;

    egl_backend(Platform& platform)
        : abstract_type(platform)
        , platform{platform}
    {
        native = wlr_gles2_renderer_get_egl(platform.renderer);

        data.base.display = wlr_egl_get_display(native);
        data.base.context = wlr_egl_get_context(native);

        load_egl_proc(&data.base.create_image_khr, "eglCreateImageKHR");
        load_egl_proc(&data.base.destroy_image_khr, "eglDestroyImageKHR");

        platform.egl_data = &data.base;

        // Egl is always direct rendering.
        this->setIsDirectRendering(true);

        gl::init_client_extensions(*this);
        gl::init_server_extensions(*this);

        for (auto& out : platform.base.all_outputs) {
            auto render = static_cast<typename Platform::output_t*>(out->render.get());
            get_egl_out(out) = std::make_unique<egl_output_t>(*render, data);
        }

        make_context_current(data);

        gl::init_gl(gl_interface::egl, get_proc_address, platform.base.x11_data.connection);
        gl::init_buffer_age(*this);
        wayland::init_egl(*this, data);

        if (this->hasExtension(QByteArrayLiteral("EGL_EXT_image_dma_buf_import"))) {
            auto const formats_set = wlr_renderer_get_dmabuf_texture_formats(platform.renderer);
            auto const formats_map = get_drm_formats<Wrapland::Server::drm_format>(formats_set);

            dmabuf = std::make_unique<Wrapland::Server::linux_dmabuf_v1>(
                platform.base.server->display.get(),
                [this](
                    auto const& planes, auto format, auto modifier, auto const& size, auto flags) {
                    return std::make_unique<Wrapland::Server::linux_dmabuf_buffer_v1>(
                        planes, format, modifier, size, flags);
                });
            dmabuf->set_formats(formats_map);
        }
    }

    ~egl_backend() override
    {
        tear_down();
    }

    void tear_down()
    {
        if (!platform.egl_data) {
            // Already cleaned up.
            return;
        }

        cleanup();

        platform.egl_data = nullptr;
        data = {};
    }

    // TODO(romangg): Is there a reasonable difference between a plain eglMakeCurrent call that this
    // function does and the override, where we set doneCurrent on the QOpenGLContext? Otherwise we
    // could merge the calls.
    void make_current()
    {
        make_context_current(data);
    }

    bool makeCurrent() override
    {
        if (auto context = QOpenGLContext::currentContext()) {
            // Workaround to tell Qt that no QOpenGLContext is current
            context->doneCurrent();
        }
        make_context_current(data);
        return is_context_current(data);
    }

    void doneCurrent() override
    {
        unset_context_current(data);
    }

    void screenGeometryChanged(QSize const& /*size*/) override
    {
        // TODO, create new buffer?
    }

    typename abstract_type::texture_priv_t*
    createBackendTexture(typename abstract_type::texture_t* texture) override
    {
        return new egl_texture<type>(texture, this);
    }

    QRegion prepareRenderingFrame() override
    {
        this->startRenderTimer();
        return QRegion();
    }

    void endRenderingFrame(QRegion const& /*rendered*/, QRegion const& /*damaged*/) override
    {
    }

    QRegion prepareRenderingForScreen(base::output* output) override
    {
        auto const& out = get_egl_out(output);

        auto native_out = static_cast<base::backend::wlroots::output*>(output)->native;
        wlr_output_attach_render(native_out, &out->bufferAge);
        wlr_renderer_begin(
            platform.renderer, output->geometry().width(), output->geometry().height());

        prepare_render_targets(*out);

        if (!this->supportsBufferAge()) {
            // If buffer age exenstion is not supported we always repaint the whole output as we
            // don't know the status of the back buffer we render to.
            return output->geometry();
        }
        if (out->render.fbo.valid()) {
            // If we render to the extra frame buffer, do not use buffer age. It leads to artifacts.
            // TODO(romangg): Can we make use of buffer age even in this case somehow?
            return output->geometry();
        }
        if (out->bufferAge == 0) {
            // If buffer age is 0, the contents of the back buffer we now will render to are
            // undefined and it has to be repainted completely.
            return output->geometry();
        }
        if (out->bufferAge > static_cast<int>(out->damageHistory.size())) {
            // If buffer age is older than our damage history has recorded we do not have all damage
            // logged for that age and we need to repaint completely.
            return output->geometry();
        }

        // But if all conditions are satisfied we can look up our damage history up until to the
        // buffer age and repaint only that.
        QRegion region;
        for (int i = 0; i < out->bufferAge - 1; i++) {
            region |= out->damageHistory[i];
        }
        return region;
    }

    void endRenderingFrameForScreen(base::output* output,
                                    QRegion const& renderedRegion,
                                    QRegion const& damagedRegion) override
    {
        auto& out = get_egl_out(output);
        auto impl_out = static_cast<base::backend::wlroots::output*>(output);

        renderFramebufferToSurface(*out);

        if (GLPlatform::instance()->supports(GLFeature::TimerQuery)) {
            out->out->last_timer_queries.emplace_back();
        }

        GLFramebuffer::popRenderTarget();
        wlr_renderer_end(platform.renderer);

        if (damagedRegion.intersected(output->geometry()).isEmpty()) {
            // If the damaged region of a window is fully occluded, the only
            // rendering done, if any, will have been to repair a reused back
            // buffer, making it identical to the front buffer.
            //
            // In this case we won't post the back buffer. Instead we'll just
            // set the buffer age to 1, so the repaired regions won't be
            // rendered again in the next frame.
            if (!renderedRegion.intersected(output->geometry()).isEmpty()) {
                glFlush();
            }

            wlr_output_rollback(impl_out->native);
            return;
        }

        set_output_damage(impl_out, damagedRegion.translated(-output->geometry().topLeft()));

        if (!out->present()) {
            out->out->swap_pending = false;
            return;
        }

        if (this->supportsBufferAge()) {
            if (out->damageHistory.size() > 10) {
                out->damageHistory.pop_back();
            }
            out->damageHistory.push_front(damagedRegion.intersected(output->geometry()));
        }
    }

    bool hasClientExtension(const QByteArray& ext) const
    {
        return data.base.client_extensions.contains(ext);
    }

    std::unique_ptr<egl_output<typename Platform::output_t>>& get_egl_out(base::output const* out)
    {
        using out_t = typename Platform::output_t;
        using base_wlout_t = base::wayland::output<base::wayland::platform>;
        return static_cast<out_t*>(static_cast<base_wlout_t const*>(out)->render.get())->egl;
    }

    Platform& platform;

    std::unique_ptr<Wrapland::Server::linux_dmabuf_v1> dmabuf;
    wayland::egl_data data;

    GLFramebuffer native_fbo;
    wlr_egl* native{nullptr};

protected:
    void present() override
    {
        // Not in use. This backend does per-screen rendering.
        Q_UNREACHABLE();
    }

private:
    void cleanup()
    {
        cleanupGL();
        doneCurrent();
        cleanupSurfaces();

        dmabuf.reset();
    }

    void cleanupSurfaces()
    {
        for (auto out : platform.base.all_outputs) {
            get_egl_out(out).reset();
        }
    }

    static void set_output_damage(base::backend::wlroots::output* output, QRegion const& src_damage)
    {
        auto damage = create_pixman_region(src_damage);

        int width, height;
        wlr_output_transformed_resolution(output->native, &width, &height);

        enum wl_output_transform transform = wlr_output_transform_invert(output->native->transform);
        wlr_region_transform(&damage, &damage, transform, width, height);

        wlr_output_set_damage(output->native, &damage);
        pixman_region32_fini(&damage);
    }

    QRect get_viewport(egl_output_t const& egl_out) const
    {
        auto const& overall = platform.base.topology.size;
        auto const& geo = egl_out.out->base.geometry();
        auto const& view = egl_out.out->base.view_geometry();

        auto const width_ratio = view.width() / static_cast<double>(geo.width());
        auto const height_ratio = view.height() / static_cast<double>(geo.height());

        return QRect(-geo.x() * width_ratio,
                     (geo.height() - overall.height() + geo.y()) * height_ratio,
                     overall.width() * width_ratio,
                     overall.height() * height_ratio);
    }

    void initRenderTarget(egl_output_t& egl_out)
    {
        if (egl_out.render.vbo) {
            // Already initialized.
            return;
        }
        std::shared_ptr<GLVertexBuffer> vbo(new GLVertexBuffer(KWin::GLVertexBuffer::Static));
        vbo->setData(6, 2, vertices, texCoords);
        egl_out.render.vbo = vbo;
    }

    void prepare_render_targets(egl_output_t& egl_out)
    {
        auto wlr_fbo = wlr_gles2_renderer_get_current_fbo(platform.renderer);
        auto const vp = get_viewport(egl_out);

        if (egl_out.render.fbo.valid()) {
            auto geo = egl_out.out->base.geometry();
            geo.moveTopLeft({});

            native_fbo = GLFramebuffer(wlr_fbo, geo);
            GLFramebuffer::pushRenderTarget(&native_fbo);

            GLFramebuffer::pushRenderTarget(&egl_out.render.fbo);
            glViewport(vp.x(), vp.y(), vp.width(), vp.height());
        } else {
            native_fbo = GLFramebuffer(wlr_fbo, vp);
            GLFramebuffer::pushRenderTarget(&native_fbo);
        }
    }

    void renderFramebufferToSurface(egl_output_t& egl_out)
    {
        if (!egl_out.render.fbo.valid()) {
            // No additional render target.
            return;
        }
        initRenderTarget(egl_out);

        GLFramebuffer::popRenderTarget();

        glClear(GL_COLOR_BUFFER_BIT);

        auto geo = egl_out.out->base.view_geometry();
        if (has_portrait_transform(egl_out.out->base)) {
            geo = geo.transposed();
            geo.moveTopLeft(geo.topLeft().transposed());
        }
        glViewport(geo.x(), geo.y(), geo.width(), geo.height());

        auto shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture);

        QMatrix4x4 rotationMatrix;
        rotationMatrix.flipCoordinates();
        rotationMatrix.rotate(
            rotation_in_degree(static_cast<base::backend::wlroots::output&>(egl_out.out->base)),
            0,
            0,
            1);
        shader->setUniform(GLShader::ModelViewProjectionMatrix, rotationMatrix);

        egl_out.render.texture->bind();
        egl_out.render.vbo->render(GL_TRIANGLES);
        ShaderManager::instance()->popShader();
    }

    static constexpr float vertices[] = {
        -1.0f,
        1.0f,
        -1.0f,
        -1.0f,
        1.0f,
        -1.0f,

        -1.0f,
        1.0f,
        1.0f,
        -1.0f,
        1.0f,
        1.0f,
    };

    static constexpr float texCoords[] = {
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,

        0.0f,
        1.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
    };
};

}
