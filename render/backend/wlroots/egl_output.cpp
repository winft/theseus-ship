/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "egl_output.h"

#include "egl_backend.h"
#include "output.h"
#include "platform.h"

#include "kwinglutils.h"
#include "wayland_logging.h"

namespace KWin::render::backend::wlroots
{

static base::backend::wlroots::output& get_base(base::wayland::output& output)
{
    return static_cast<base::backend::wlroots::output&>(output);
}

egl_output::egl_output(output& out, egl_backend* egl_back)
    : out{&out}
    , egl_back{egl_back}
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
    egl_back = other.egl_back;
    damageHistory = std::move(other.damageHistory);

    render = other.render;
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

    glGenFramebuffers(1, &render.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, render.framebuffer);

    glGenTextures(1, &render.texture);
    glBindTexture(GL_TEXTURE_2D, render.texture);

    auto const texSize = view_geo.size();

    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 texSize.width(),
                 texSize.height(),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render.texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        qCWarning(KWIN_WL) << "Framebuffer not complete";
        return false;
    }

    return true;
}

void egl_output::cleanup_framebuffer()
{
    if (!render.framebuffer) {
        return;
    }
    make_current();
    glDeleteTextures(1, &render.texture);
    render.texture = 0;
    glDeleteFramebuffers(1, &render.framebuffer);
    render.framebuffer = 0;
}

void egl_output::make_current() const
{
    wlr_egl_make_current(egl_back->native);
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
