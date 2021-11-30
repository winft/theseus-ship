/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "platformsupport/scenes/opengl/abstract_egl_backend.h"

#include <deque>
#include <memory>

namespace KWin::render::backend::wlroots
{

class backend;
class egl_gbm;
class egl_output;
class output;
class surface;

class egl_backend : public gl::egl_backend
{
    Q_OBJECT
private:
    bool init_platform();
    bool init_rendering_context();

    void add_output(output* out);

    void setViewport(egl_output const& egl_out) const;

    void initRenderTarget(egl_output& egl_out);

    void prepareRenderFramebuffer(egl_output const& egl_out) const;
    void renderFramebufferToSurface(egl_output& egl_out);

public:
    std::unique_ptr<egl_gbm> gbm;
    wlroots::backend* back;
    std::vector<egl_output> outputs;
    bool headless{false};
    std::unique_ptr<wlroots::surface> dummy_surface;

    egl_backend(wlroots::backend* back, bool headless);
    ~egl_backend() override;

    void init() override;

    void screenGeometryChanged(QSize const& size) override;
    gl::texture_private* createBackendTexture(gl::texture* texture) override;

    QRegion prepareRenderingFrame() override;
    void endRenderingFrame(QRegion const& renderedRegion, QRegion const& damagedRegion) override;

    void endRenderingFrameForScreen(base::output* output,
                                    QRegion const& damage,
                                    QRegion const& damagedRegion) override;
    QRegion prepareRenderingForScreen(base::output* output) override;

    bool usesOverlayWindow() const override;

    egl_output& get_output(base::output* out);

protected:
    void present() override;
    void cleanupSurfaces() override;
};

class egl_texture : public gl::egl_texture
{
public:
    ~egl_texture() override;

private:
    friend class egl_backend;
    egl_texture(gl::texture* texture, egl_backend* backend);
};

}
