/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "egl_backend.h"

#include "egl_output.h"
#include "egl_texture.h"
#include "output.h"
#include "platform.h"
#include "wlr_helpers.h"

#include "base/backend/wlroots/output.h"
#include "render/gl/egl.h"
#include "render/gl/gl.h"
#include "render/wayland/egl.h"
#include "wayland_logging.h"
#include "win/space.h"

#include <kwingl/platform.h>

#include <QOpenGLContext>
#include <stdexcept>

namespace KWin::render::backend::wlroots
{

using eglFuncPtr = void (*)();
static eglFuncPtr get_proc_address(char const* name)
{
    return eglGetProcAddress(name);
}

std::unique_ptr<egl_output>& egl_backend::get_egl_out(base::output const* out)
{
    return static_cast<output*>(static_cast<base::wayland::output const*>(out)->render.get())->egl;
}

egl_backend::egl_backend(wlroots::platform& platform)
    : gl::backend()
    , platform{platform}
{
    native = wlr_gles2_renderer_get_egl(platform.renderer);

    data.base.display = native->display;
    data.base.context = native->context;

    data.base.create_image_khr = native->procs.eglCreateImageKHR;
    data.base.destroy_image_khr = native->procs.eglDestroyImageKHR;

    platform.egl_data = &data.base;

    // Egl is always direct rendering.
    setIsDirectRendering(true);

    gl::init_client_extensions(*this);
    gl::init_server_extensions(*this);

    for (auto& out : platform.base.all_outputs) {
        auto render = static_cast<output*>(static_cast<base::wayland::output*>(out)->render.get());
        get_egl_out(out) = std::make_unique<egl_output>(*render, this);
    }

    wlr_egl_make_current(native);

    gl::init_gl(EglPlatformInterface, get_proc_address);
    gl::init_buffer_age(*this);
    wayland::init_egl(*this, data);
}

egl_backend::~egl_backend()
{
    tear_down();
}

bool egl_backend::hasClientExtension(const QByteArray& ext) const
{
    return data.base.client_extensions.contains(ext);
}

void egl_backend::tear_down()
{
    if (!platform.egl_data) {
        // Already cleaned up.
        return;
    }

    cleanup();

    platform.egl_data = nullptr;
    data = {};
}

void egl_backend::cleanup()
{
    cleanupGL();
    doneCurrent();
    cleanupSurfaces();

    delete dmabuf;
    dmabuf = nullptr;
}

void egl_backend::cleanupSurfaces()
{
    for (auto out : platform.base.all_outputs) {
        get_egl_out(out).reset();
    }
}

void egl_backend::present()
{
    // Not in use. This backend does per-screen rendering.
    Q_UNREACHABLE();
}

bool egl_backend::makeCurrent()
{
    if (auto context = QOpenGLContext::currentContext()) {
        // Workaround to tell Qt that no QOpenGLContext is current
        context->doneCurrent();
    }
    wlr_egl_make_current(native);
    return wlr_egl_is_current(native);
}

void egl_backend::doneCurrent()
{
    wlr_egl_unset_current(native);
}

void egl_backend::screenGeometryChanged(QSize const& size)
{
    Q_UNUSED(size)
    // TODO, create new buffer?
}

gl::texture_private* egl_backend::createBackendTexture(gl::texture* texture)
{
    return new egl_texture(texture, this);
}

QRegion egl_backend::prepareRenderingFrame()
{
    startRenderTimer();
    return QRegion();
}

QRegion egl_backend::prepareRenderingForScreen(base::output* output)
{
    auto const& out = get_egl_out(output);

    auto native_out = static_cast<base::backend::wlroots::output*>(output)->native;
    wlr_output_attach_render(native_out, &out->bufferAge);
    wlr_renderer_begin(platform.renderer, output->geometry().width(), output->geometry().height());

    native_fbo
        = GLRenderTarget(wlr_gles2_renderer_get_current_fbo(platform.renderer), get_viewport(*out));
    GLRenderTarget::pushRenderTarget(&native_fbo);

    QMatrix4x4 flip_180;
    flip_180(1, 1) = -1;
    transformation = flip_180;

    prepareRenderFramebuffer(*out);

    if (!supportsBufferAge()) {
        // If buffer age exenstion is not supported we always repaint the whole output as we don't
        // know the status of the back buffer we render to.
        return output->geometry();
    }
    if (out->render.fbo.valid()) {
        // If we render to the extra frame buffer, do not use buffer age. It leads to artifacts.
        // TODO(romangg): Can we make use of buffer age even in this case somehow?
        return output->geometry();
    }
    if (out->bufferAge == 0) {
        // If buffer age is 0, the contents of the back buffer we now will render to are undefined
        // and it has to be repainted completely.
        return output->geometry();
    }
    if (out->bufferAge > static_cast<int>(out->damageHistory.size())) {
        // If buffer age is older than our damage history has recorded we do not have all damage
        // logged for that age and we need to repaint completely.
        return output->geometry();
    }

    // But if all conditions are satisfied we can look up our damage history up until to the buffer
    // age and repaint only that.
    QRegion region;
    for (int i = 0; i < out->bufferAge - 1; i++) {
        region |= out->damageHistory[i];
    }
    return region;
}

void egl_backend::endRenderingFrame(QRegion const& renderedRegion, QRegion const& damagedRegion)
{
    Q_UNUSED(renderedRegion)
    Q_UNUSED(damagedRegion)
}

void set_output_damage(base::backend::wlroots::output* output, QRegion const& src_damage)
{
    auto damage = create_pixman_region(src_damage);

    int width, height;
    wlr_output_transformed_resolution(output->native, &width, &height);

    enum wl_output_transform transform = wlr_output_transform_invert(output->native->transform);
    wlr_region_transform(&damage, &damage, transform, width, height);

    wlr_output_set_damage(output->native, &damage);
    pixman_region32_fini(&damage);
}

void egl_backend::endRenderingFrameForScreen(base::output* output,
                                             QRegion const& renderedRegion,
                                             QRegion const& damagedRegion)
{
    auto& out = get_egl_out(output);
    auto impl_out = static_cast<base::backend::wlroots::output*>(output);

    renderFramebufferToSurface(*out);

    if (GLPlatform::instance()->supports(GLFeature::TimerQuery)) {
        out->out->last_timer_queries.emplace_back();
    }

    GLRenderTarget::popRenderTarget();
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

    if (supportsBufferAge()) {
        if (out->damageHistory.size() > 10) {
            out->damageHistory.pop_back();
        }
        out->damageHistory.push_front(damagedRegion.intersected(output->geometry()));
    }
}

void egl_backend::prepareRenderFramebuffer(egl_output& egl_out) const
{
    if (egl_out.render.fbo.valid()) {
        GLRenderTarget::pushRenderTarget(&egl_out.render.fbo);
    }
}

QRect egl_backend::get_viewport(egl_output const& egl_out) const
{
    auto const& overall = platform.base.topology.size;
    auto const& geo = egl_out.out->base.geometry();
    auto const& view = egl_out.out->base.view_geometry();

    auto const width_ratio = view.width() / (double)geo.width();
    auto const height_ratio = view.height() / (double)geo.height();

    return QRect(-geo.x() * width_ratio,
                 -geo.y() * height_ratio,
                 overall.width() * width_ratio,
                 overall.height() * height_ratio);
}

const float vertices[] = {
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

const float texCoords[] = {
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

void egl_backend::initRenderTarget(egl_output& egl_out)
{
    if (egl_out.render.vbo) {
        // Already initialized.
        return;
    }
    std::shared_ptr<GLVertexBuffer> vbo(new GLVertexBuffer(KWin::GLVertexBuffer::Static));
    vbo->setData(6, 2, vertices, texCoords);
    egl_out.render.vbo = vbo;
}

void egl_backend::renderFramebufferToSurface(egl_output& egl_out)
{
    if (!egl_out.render.fbo.valid()) {
        // No additional render target.
        return;
    }
    initRenderTarget(egl_out);

    GLRenderTarget::popRenderTarget();

    glClear(GL_COLOR_BUFFER_BIT);

    auto geo = egl_out.out->base.view_geometry();
    if (has_portrait_transform(egl_out.out->base)) {
        geo = geo.transposed();
        geo.moveTopLeft(geo.topLeft().transposed());
    }
    glViewport(geo.x(), geo.y(), geo.width(), geo.height());

    auto shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture);

    QMatrix4x4 rotationMatrix;
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

}
