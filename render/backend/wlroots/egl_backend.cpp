/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "egl_backend.h"

#include "backend.h"
#include "egl_helpers.h"
#include "egl_output.h"
#include "output.h"
#include "surface.h"
#include "wlr_helpers.h"

#include "render/wayland/compositor.h"
#include "render/wayland/output.h"
#include "screens.h"

#include <kwinglplatform.h>
#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

egl_output& egl_backend::get_output(base::output* out)
{
    auto it = std::find_if(
        outputs.begin(), outputs.end(), [this, out](auto& egl_out) { return egl_out.out == out; });
    assert(it != outputs.end());
    return *it;
}

egl_backend::egl_backend(wlroots::backend* back, bool headless)
    : gl::egl_backend()
    , back{back}
    , headless{headless}
{
    // Egl is always direct rendering.
    setIsDirectRendering(true);

    connect(back, &wlroots::backend::output_added, this, [this](auto out) {
        add_output(static_cast<output*>(out));
    });
    connect(back, &wlroots::backend::output_removed, this, [this](auto out) {
        outputs.erase(std::remove_if(outputs.begin(),
                                     outputs.end(),
                                     [&out](auto& egl_out) { return egl_out.out == out; }),
                      outputs.end());
    });
}

egl_backend::~egl_backend()
{
    outputs.clear();
    cleanup();
}

void egl_backend::init()
{
    initClientExtensions();

    if (!init_platform()) {
        setFailed("Could not initialize EGL backend.");
        return;
    }
    if (!initEglAPI()) {
        setFailed("Could not initialize EGL API.");
        return;
    }
    if (!init_buffer_configs(this)) {
        setFailed("Could not initialize buffer configs.");
        return;
    }
    if (!init_rendering_context()) {
        setFailed("Could not initialize rendering context");
        return;
    }

    initKWinGL();
    initBufferAge();
    initWayland();
}

bool egl_backend::init_platform()
{
    if (headless) {
        auto egl_display = get_egl_headless(*back);
        if (egl_display == EGL_NO_DISPLAY) {
            return false;
        }
        setEglDisplay(egl_display);
        return true;
    }

    auto gbm = get_egl_gbm(*back);
    if (!gbm) {
        return false;
    }

    assert(gbm->egl_display != EGL_NO_DISPLAY);
    setEglDisplay(gbm->egl_display);

    this->gbm = std::move(gbm);
    return true;
}

bool egl_backend::init_rendering_context()
{
    if (!createContext()) {
        return false;
    }

    for (auto out : back->all_outputs) {
        add_output(out);
    }

    // AbstractEglBackend expects a surface to be set but this is not relevant as we render per
    // output and make the context current here on that output's surface. For simplicity we just
    // create a dummy surface and keep that constantly set over the run time.
    dummy_surface = headless ? create_headless_surface(*back, QSize(800, 600))
                             : create_surface(*back, QSize(800, 600));
    setSurface(dummy_surface->egl);

    if (outputs.empty()) {
        // In case no outputs are connected make the context current with our dummy surface.
        return make_current(dummy_surface->egl, this);
    }

    return outputs.front().make_current();
}

void egl_backend::add_output(output* out)
{
    auto egl_out = egl_output(out, this);
    if (!egl_out.reset(out)) {
        return;
    }

    outputs.push_back(std::move(egl_out));

    connect(out, &output::mode_changed, this, [out, this] {
        auto& egl_out = get_output(out);
        egl_out.reset(out);
    });
}

void egl_backend::cleanupSurfaces()
{
    outputs.clear();
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
    if (!egl_out.render.framebuffer) {
        // No additional render target.
        return;
    }
    initRenderTarget(egl_out);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLRenderTarget::setKWinFramebuffer(0);

    GLuint clearColor[4] = {0, 0, 0, 0};
    glClearBufferuiv(GL_COLOR, 0, clearColor);

    auto geo = egl_out.out->view_geometry();
    if (has_portrait_transform(egl_out.out)) {
        geo = geo.transposed();
        geo.moveTopLeft(geo.topLeft().transposed());
    }
    glViewport(geo.x(), geo.y(), geo.width(), geo.height());

    auto shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture);

    QMatrix4x4 rotationMatrix;
    rotationMatrix.rotate(rotation_in_degree(egl_out.out), 0, 0, 1);
    shader->setUniform(GLShader::ModelViewProjectionMatrix, rotationMatrix);

    glBindTexture(GL_TEXTURE_2D, egl_out.render.texture);
    egl_out.render.vbo->render(GL_TRIANGLES);
    ShaderManager::instance()->popShader();
}

void egl_backend::prepareRenderFramebuffer(egl_output const& egl_out) const
{
    // When render.framebuffer is 0 we may just reset to the screen framebuffer.
    glBindFramebuffer(GL_FRAMEBUFFER, egl_out.render.framebuffer);
    GLRenderTarget::setKWinFramebuffer(egl_out.render.framebuffer);
}

void egl_backend::present()
{
    // Not in use. This backend does per-screen rendering.
    Q_UNREACHABLE();
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

void egl_backend::setViewport(egl_output const& egl_out) const
{
    auto const& overall = Screens::self()->size();
    auto const& geo = egl_out.out->geometry();
    auto const& view = egl_out.out->view_geometry();

    auto const width_ratio = view.width() / (double)geo.width();
    auto const height_ratio = view.height() / (double)geo.height();

    glViewport(-geo.x() * width_ratio,
               (geo.height() - overall.height() + geo.y()) * height_ratio,
               overall.width() * width_ratio,
               overall.height() * height_ratio);
}

QRegion egl_backend::prepareRenderingForScreen(base::output* output)
{
    auto const& out = get_output(output);

    out.make_current();
    prepareRenderFramebuffer(out);
    setViewport(out);

    if (!supportsBufferAge()) {
        // If buffer age exenstion is not supported we always repaint the whole output as we don't
        // know the status of the back buffer we render to.
        return output->geometry();
    }
    if (out.render.framebuffer) {
        // If we render to the extra frame buffer, do not use buffer age. It leads to artifacts.
        // TODO(romangg): Can we make use of buffer age even in this case somehow?
        return output->geometry();
    }
    if (out.bufferAge == 0) {
        // If buffer age is 0, the contents of the back buffer we now will render to are undefined
        // and it has to be repainted completely.
        return output->geometry();
    }
    if (out.bufferAge > static_cast<int>(out.damageHistory.size())) {
        // If buffer age is older than our damage history has recorded we do not have all damage
        // logged for that age and we need to repaint completely.
        return output->geometry();
    }

    // But if all conditions are satisfied we can look up our damage history up until to the buffer
    // age and repaint only that.
    QRegion region;
    for (int i = 0; i < out.bufferAge - 1; i++) {
        region |= out.damageHistory[i];
    }
    return region;
}

void egl_backend::endRenderingFrame(QRegion const& renderedRegion, QRegion const& damagedRegion)
{
    Q_UNUSED(renderedRegion)
    Q_UNUSED(damagedRegion)
}

void egl_backend::endRenderingFrameForScreen(base::output* output,
                                             QRegion const& renderedRegion,
                                             QRegion const& damagedRegion)
{
    auto& out = get_output(output);
    renderFramebufferToSurface(out);

    auto compositor = static_cast<wayland::compositor*>(back->compositor);
    auto render_output = compositor->outputs.at(out.out).get();
    if (GLPlatform::instance()->supports(GLFeature::TimerQuery)) {
        render_output->last_timer_queries.emplace_back();
    }

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

        out.bufferAge = 1;
        return;
    }

    eglSwapBuffers(eglDisplay(), out.surf->egl);
    auto buffer = out.create_buffer();

    if (!out.present(buffer)) {
        out.bufferAge = 0;
        render_output->swap_pending = false;
        return;
    }

    if (supportsBufferAge()) {
        eglQuerySurface(eglDisplay(), out.surf->egl, EGL_BUFFER_AGE_EXT, &out.bufferAge);

        if (out.damageHistory.size() > 10) {
            out.damageHistory.pop_back();
        }
        out.damageHistory.push_front(damagedRegion.intersected(output->geometry()));
    }
}

egl_texture::egl_texture(gl::texture* texture, egl_backend* backend)
    : gl::egl_texture(texture, backend)
{
}

egl_texture::~egl_texture() = default;

}
