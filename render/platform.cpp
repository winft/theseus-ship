/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "compositor.h"
#include "effects.h"
#include "outline.h"
#include "post/night_color_manager.h"
#include "scene.h"
#include "singleton_interface.h"

#include "base/logging.h"
#include "config-kwin.h"
#include "main.h"

namespace KWin::render
{

platform::platform(base::platform& base)
    : night_color{std::make_unique<render::post::night_color_manager>()}
    , base{base}
{
    singleton_interface::platform = this;
}

platform::~platform()
{
    singleton_interface::platform = nullptr;
}

render::gl::backend* platform::get_opengl_backend(render::compositor& /*compositor*/)
{
    return nullptr;
}

render::qpainter::backend* platform::get_qpainter_backend(render::compositor& /*compositor*/)
{
    return nullptr;
}

}
