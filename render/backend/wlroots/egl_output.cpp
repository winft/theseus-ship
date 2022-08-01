/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "egl_output.h"

#include "egl_helpers.h"
#include "output.h"
#include "platform.h"

#include "wayland_logging.h"

#include <kwingl/utils.h>

namespace KWin::render::backend::wlroots
{

static base::backend::wlroots::output& get_base(base::wayland::output& output)
{
    return static_cast<base::backend::wlroots::output&>(output);
}

egl_output::egl_output(output& out, wayland::egl_data egl_data)
    : out{&out}
    , egl_data{egl_data}
{
    reset();
}

egl_output::egl_output(egl_output&& other) noexcept
{
    *this = std::move(other);
}

egl_output& egl_output::operator=(egl_output&& other) noexcept
{
    out = other.out;
    bufferAge = other.bufferAge;
    egl_data = std::move(other.egl_data);
    damageHistory = std::move(other.damageHistory);

    render = std::move(other.render);
    other.render = {};

    return *this;
}

egl_output::~egl_output()
{
    cleanup_framebuffer();
}

bool egl_output::reset()
{
    reset_framebuffer();
    return true;
}

bool egl_output::reset_framebuffer()
{
    cleanup_framebuffer();

    auto const view_geo = out->base.view_geometry();
    auto const centered_view
        = out->base.mode_size() != view_geo.size() || !view_geo.topLeft().isNull();

    if (out->base.transform() == base::wayland::output_transform::normal && !centered_view) {
        // No need to create intermediate framebuffer.
        return true;
    }

    // TODO(romangg): Also return in case wlroots can rotate in hardware.

    make_current();

    auto const texSize = view_geo.size();
    render.texture = GLTexture(GL_TEXTURE_2D, texSize.width(), texSize.height());
    render.fbo = GLRenderTarget(render.texture.value());
    return render.fbo.valid();
}

void egl_output::cleanup_framebuffer()
{
    if (!render.fbo.valid()) {
        return;
    }
    make_current();
    render.texture = {};
    render.fbo = {};
}

void egl_output::make_current() const
{
    make_context_current(egl_data);
}

bool egl_output::present()
{
    auto& base = get_base(out->base);
    out->swap_pending = true;

    if (!base.native->enabled) {
        wlr_output_enable(base.native, true);
    }

    if (!wlr_output_test(base.native)) {
        qCWarning(KWIN_WL) << "Atomic output test failed on present.";
        wlr_output_rollback(base.native);
        reset();
        return false;
    }
    if (!wlr_output_commit(base.native)) {
        qCWarning(KWIN_WL) << "Atomic output commit failed on present.";
        reset();
        return false;
    }
    return true;
}

}
