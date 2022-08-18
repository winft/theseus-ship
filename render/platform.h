/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "gl/egl_data.h"
#include "post/night_color_manager.h"
#include "singleton_interface.h"

#include "kwinglobals.h"

#include <memory>

namespace KWin::render
{

template<typename Base>
class platform
{
public:
    using base_t = Base;
    using platform_t = platform<base_t>;
    using space_t = typename base_t::space_t;

    virtual ~platform()
    {
        singleton_interface::get_egl_data = {};
    }

    // TODO(romangg): Remove the boolean trap.
    virtual void render_stop(bool on_shutdown) = 0;

    virtual bool requiresCompositing() const = 0;
    virtual bool compositingPossible() const = 0;
    virtual QString compositingNotPossibleReason() const = 0;
    virtual bool openGLCompositingIsBroken() const = 0;
    virtual void createOpenGLSafePoint(OpenGLSafePoint safePoint) = 0;

    /**
     * Platform specific way to invert the screen.
     * Default implementation invokes the invert effect
     */
    virtual void invertScreen() = 0;

    virtual CompositingType selected_compositor() const = 0;

    std::unique_ptr<render::post::night_color_manager> night_color;
    Base& base;

    gl::egl_data* egl_data{nullptr};

protected:
    platform(Base& base)
        : night_color{std::make_unique<render::post::night_color_manager>()}
        , base{base}
    {
        singleton_interface::get_egl_data = [this] { return egl_data; };
    }
};

}
