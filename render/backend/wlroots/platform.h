/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include "base/backend/wlroots/output.h"
#include "base/backend/wlroots/platform.h"
#include "base/utils.h"
#include "render/wayland/platform.h"

#include <variant>

namespace KWin
{

namespace render::backend::wlroots
{

template<typename Platform>
class egl_backend;

template<typename Platform>
class qpainter_backend;

class KWIN_EXPORT platform : public wayland::platform
{
public:
    explicit platform(base::backend::wlroots::platform& base);
    ~platform() override;

    void init();

    CompositingType selected_compositor() const override;

    gl::backend* get_opengl_backend(render::compositor& compositor) override;
    qpainter::backend* get_qpainter_backend(render::compositor& compositor) override;
    void render_stop(bool on_shutdown) override;

    base::backend::wlroots::platform& base;
    std::unique_ptr<egl_backend<platform>> egl;
    std::unique_ptr<qpainter_backend<platform>> qpainter;

    wlr_renderer* renderer{nullptr};
    wlr_allocator* allocator{nullptr};
};

}
}
