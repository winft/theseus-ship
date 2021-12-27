/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include "base/backend/wlroots/output.h"
#include "base/backend/wlroots/platform.h"
#include "base/utils.h"
#include "render/platform.h"

#include <Wrapland/Server/drm_lease_v1.h>
#include <variant>

struct gbm_device;

namespace KWin
{

namespace render::backend::wlroots
{

class egl_backend;

class KWIN_EXPORT platform : public render::platform
{
    Q_OBJECT
public:
    base::backend::wlroots::platform& base;
    std::unique_ptr<egl_backend> egl;

#if HAVE_WLR_OUTPUT_INIT_RENDER
    wlr_renderer* renderer{nullptr};
    wlr_allocator* allocator{nullptr};
#endif

    explicit platform(base::backend::wlroots::platform& base);
    ~platform() override;

    gl::backend* createOpenGLBackend(render::compositor& compositor) override;
    void render_stop(bool on_shutdown) override;

    void createEffectsHandler(render::compositor* compositor, render::scene* scene) override;

    void init();

    QVector<CompositingType> supportedCompositors() const override;

    // Needed for final cleanup on platform destroy.
    // TODO(romangg): Can we make this unnecessary.
    EGLDisplay egl_display_to_terminate{EGL_NO_DISPLAY};

private:
    void init_drm_leasing();
    void process_drm_leased(Wrapland::Server::drm_lease_v1* lease);

    clockid_t m_clockId;
    base::event_receiver<platform> new_output;
};

}
}
