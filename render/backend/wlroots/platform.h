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

#include <variant>

struct gbm_device;

namespace KWin
{

namespace render::backend::wlroots
{

class egl_backend;
class qpainter_backend;

class KWIN_EXPORT platform : public render::platform
{
    Q_OBJECT
public:
    explicit platform(base::backend::wlroots::platform& base);
    ~platform() override;

    void init();
    void createEffectsHandler(render::compositor* compositor, render::scene* scene) override;
    CompositingType selected_compositor() const override;

    gl::backend* get_opengl_backend(render::compositor& compositor) override;
    qpainter::backend* get_qpainter_backend(render::compositor& compositor) override;
    void render_stop(bool on_shutdown) override;

    base::backend::wlroots::platform& base;
    std::unique_ptr<egl_backend> egl;
    std::unique_ptr<qpainter_backend> qpainter;

    wlr_renderer* renderer{nullptr};
    wlr_allocator* allocator{nullptr};
};

}
}
