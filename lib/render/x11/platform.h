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

#include <KConfigGroup>
#include <memory>

namespace KWin::render::x11
{

template<typename Base>
class platform : public render::platform<Base>
{
public:
    using base_t = Base;
    using type = platform<base_t>;
    using space_t = typename base_t::space_t;
    using compositor_t = typename x11::compositor<type>;
    using scene_t = typename compositor_t::scene_t;

    using window_t = typename scene_t::window_t;
    using buffer_t = x11::buffer_win_integration<typename scene_t::buffer_t>;

    platform(Base& base)
        : render::platform<Base>(base)
    {
    }

    virtual gl::backend<gl::scene<type>, type>* get_opengl_backend(compositor_t& compositor) = 0;
    virtual render::outline_visual* create_non_composited_outline(render::outline* outline) = 0;

    std::unique_ptr<win::deco::render_injector>
    create_non_composited_deco(win::deco::render_window window)
    {
        return std::make_unique<backend::x11::deco_renderer>(this->base.x11_data, window);
    }

    bool requiresCompositing() const override
    {
        return false;
    }

    bool openGLCompositingIsBroken() const override
    {
        const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
        return KConfigGroup(this->base.config.main, "Compositing").readEntry(unsafeKey, false);
    }

    std::unique_ptr<compositor_t> compositor;
};

}
