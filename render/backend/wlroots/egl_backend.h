/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/gl/backend.h"
#include "render/wayland/egl_data.h"

#include <kwingl/utils.h>

#include <memory>

struct wlr_egl;

namespace KWin::render
{

namespace gl
{
class egl_dmabuf;
}

namespace backend::wlroots
{

class platform;
class egl_output;
class output;

class egl_backend : public gl::backend
{
public:
    egl_backend(wlroots::platform& platform);
    ~egl_backend() override;

    void tear_down();

    // TODO(romangg): Is there a reasonable difference between a plain eglMakeCurrent call that this
    // function does and the override, where we set doneCurrent on the QOpenGLContext? Otherwise we
    // could merge the calls.
    void make_current();
    bool makeCurrent() override;
    void doneCurrent() override;

    void screenGeometryChanged(QSize const& size) override;
    gl::texture_private* createBackendTexture(gl::texture* texture) override;

    QRegion prepareRenderingFrame() override;
    void endRenderingFrame(QRegion const& renderedRegion, QRegion const& damagedRegion) override;

    QRegion prepareRenderingForScreen(base::output* output) override;
    void endRenderingFrameForScreen(base::output* output,
                                    QRegion const& damage,
                                    QRegion const& damagedRegion) override;

    bool hasClientExtension(const QByteArray& ext) const;
    std::unique_ptr<egl_output>& get_egl_out(base::output const* out);

    wlroots::platform& platform;

    gl::egl_dmabuf* dmabuf{nullptr};
    wayland::egl_data data;

    GLRenderTarget native_fbo;
    wlr_egl* native{nullptr};

protected:
    void present() override;

private:
    void cleanup();
    void cleanupSurfaces();

    QRect get_viewport(egl_output const& egl_out) const;
    void initRenderTarget(egl_output& egl_out);

    void prepareRenderFramebuffer(egl_output& egl_out) const;
    void renderFramebufferToSurface(egl_output& egl_out);
};

}
}
