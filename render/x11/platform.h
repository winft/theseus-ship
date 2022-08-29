/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor.h"
#include "effects.h"

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

    platform(Base& base)
        : render::platform<Base>(base)
    {
    }

    virtual gl::backend<gl::scene<type>, type>* get_opengl_backend(compositor_t& compositor) = 0;
    virtual render::outline_visual* create_non_composited_outline(render::outline* outline) = 0;
    virtual win::deco::renderer<win::deco::client_impl<typename space_t::window_t>>*
    createDecorationRenderer(win::deco::client_impl<typename space_t::window_t>* client)
        = 0;

    bool requiresCompositing() const override
    {
        return false;
    }

    bool openGLCompositingIsBroken() const override
    {
        const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
        return KConfigGroup(kwinApp()->config(), "Compositing").readEntry(unsafeKey, false);
    }

    std::unique_ptr<render::effects_handler_impl<compositor_t>> createEffectsHandler()
    {
        return std::make_unique<x11::effects_handler_impl<compositor_t>>(*compositor);
    }

    std::unique_ptr<compositor_t> compositor;
};

}
