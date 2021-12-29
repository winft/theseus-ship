/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/gl/backend.h"
#include "render/wayland/egl_data.h"

#include <memory>

namespace KWin::render
{

namespace gl
{
class egl_dmabuf;
}

namespace backend::wlroots
{

class platform;
class egl_gbm;
class egl_output;
class output;
class surface;

class egl_backend : public gl::backend
{
public:
    egl_backend(wlroots::platform& platform, bool headless);
    ~egl_backend() override;

    void start();
    void tear_down();

    void cleanup();
    void cleanupSurfaces();

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

    std::unique_ptr<egl_gbm> gbm;
    wlroots::platform& platform;
    bool headless{false};
    std::unique_ptr<wlroots::surface> dummy_surface;

    gl::egl_dmabuf* dmabuf{nullptr};
    wayland::egl_data data;

protected:
    void present() override;

private:
    bool init_platform();
    bool init_rendering_context();

    void setViewport(egl_output const& egl_out) const;
    void initRenderTarget(egl_output& egl_out);

    void prepareRenderFramebuffer(egl_output const& egl_out) const;
    void renderFramebufferToSurface(egl_output& egl_out);
};

}
}
