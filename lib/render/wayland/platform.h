/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor.h"
#include "effects.h"

#include "render/platform.h"
#include <render/gl/egl_data.h>
#include <render/options.h>
#include <render/post/night_color_manager.h>
#include <render/singleton_interface.h>

#include <memory>

namespace KWin::render::wayland
{

template<typename Base>
class platform : public render::platform
{
public:
    using type = platform<Base>;
    using base_t = Base;
    using compositor_t = typename wayland::compositor<platform<Base>>;
    using scene_t = typename compositor_t::scene_t;
    using space_t = typename Base::space_t;
    using effect_window_group_t = effect_window_group_impl<typename space_t::window_group_t>;

    using window_t = typename scene_t::window_t;
    using buffer_t = buffer_win_integration<typename scene_t::buffer_t>;

    platform(Base& base)
        : base{base}
        , options{std::make_unique<render::options>(base.operation_mode, base.config.main)}
        , night_color{std::make_unique<render::post::night_color_manager<Base>>(base)}
    {
        singleton_interface::get_egl_data = [this] { return egl_data; };
    }

    ~platform() override
    {
        singleton_interface::get_egl_data = {};
    }

    bool requiresCompositing() const
    {
        return true;
    }

    bool compositingPossible() const
    {
        return true;
    }

    QString compositingNotPossibleReason() const
    {
        return {};
    }

    bool openGLCompositingIsBroken() const
    {
        return false;
    }

    void createOpenGLSafePoint(opengl_safe_point /*safePoint*/)
    {
    }

    render::outline_visual* create_non_composited_outline(render::outline* /*outline*/)
    {
        // Not possible on Wayland.
        return nullptr;
    }

    void invertScreen()
    {
        assert(compositor->effects);
        compositor->effects->invert_screen();
    }

    virtual gl::backend<gl::scene<compositor_t>, type>* get_opengl_backend(compositor_t& compositor)
        = 0;
    virtual qpainter::backend<qpainter::scene<compositor_t>>*
    get_qpainter_backend(compositor_t& compositor)
        = 0;
    virtual bool is_sw_compositing() const = 0;

    // TODO(romangg): Remove the boolean trap.
    virtual void render_stop(bool on_shutdown) = 0;

    Base& base;
    std::unique_ptr<render::options> options;
    gl::egl_data* egl_data{nullptr};

    std::unique_ptr<render::post::night_color_manager<Base>> night_color;
    std::unique_ptr<compositor_t> compositor;

    int output_index{0};
};

}
