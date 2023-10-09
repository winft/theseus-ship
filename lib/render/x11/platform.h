/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "buffer.h"
#include "compositor.h"
#include "effects.h"

#include "render/backend/x11/deco_renderer.h"
#include "render/gl/backend.h"
#include "render/platform.h"
#include <render/gl/egl_data.h>
#include <render/options.h>
#include <render/post/night_color_manager.h>
#include <render/singleton_interface.h>

#include <KConfigGroup>
#include <memory>

namespace KWin::render::x11
{

template<typename Base>
class platform : public render::platform
{
public:
    using type = platform<Base>;
    using base_t = Base;
    using compositor_t = typename x11::compositor<type>;
    using scene_t = typename compositor_t::scene_t;
    using space_t = typename base_t::space_t;
    using effect_window_group_t = effect_window_group_impl<typename space_t::window_group_t>;

    using window_t = typename scene_t::window_t;
    using buffer_t = x11::buffer_win_integration<typename scene_t::buffer_t>;

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

    virtual gl::backend<gl::scene<compositor_t>, type>* get_opengl_backend(compositor_t& compositor)
        = 0;
    virtual render::outline_visual* create_non_composited_outline(render::outline* outline) = 0;

    std::unique_ptr<win::deco::render_injector>
    create_non_composited_deco(win::deco::render_window window)
    {
        return std::make_unique<backend::x11::deco_renderer>(this->base.x11_data, window);
    }

    bool requiresCompositing() const
    {
        return false;
    }

    bool openGLCompositingIsBroken() const
    {
        const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
        return KConfigGroup(this->base.config.main, "Compositing").readEntry(unsafeKey, false);
    }

    virtual bool compositingPossible() const = 0;
    virtual QString compositingNotPossibleReason() const = 0;
    virtual void createOpenGLSafePoint(opengl_safe_point safePoint) = 0;
    virtual void invertScreen() = 0;
    virtual bool is_sw_compositing() const = 0;

    // TODO(romangg): Remove the boolean trap.
    virtual void render_stop(bool on_shutdown) = 0;

    Base& base;
    std::unique_ptr<render::options> options;
    gl::egl_data* egl_data{nullptr};

    std::unique_ptr<render::post::night_color_manager<Base>> night_color;
    std::unique_ptr<compositor_t> compositor;
};

}
