/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "egl_output.h"

#include "backend.h"
#include "composite.h"
#include "egl_backend.h"
#include "egl_helpers.h"
#include "output.h"
#include "surface.h"

#include "render/wayland/output.h"
#include "screens.h"

#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

egl_output::egl_output(output* out, egl_backend* egl_back)
    : out{out}
    , egl_back{egl_back}
{
}

egl_output::egl_output(egl_output&& other) noexcept
{
    *this = std::move(other);
}

egl_output& egl_output::operator=(egl_output&& other) noexcept
{
    out = other.out;
    surf = std::move(other.surf);
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

buffer* egl_output::create_buffer()
{
    return new buffer(surf.get());
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

bool egl_output::reset(output* out)
{
    this->out = out;

    auto size = out->modeSize();

    auto surf = create_surface(*egl_back->back, size);
    if (!surf) {
        qCWarning(KWIN_WL) << "Not able to create surface on output reset.";
        return false;
    }

    if (this->surf && egl_back->surface() == this->surf->egl) {
        egl_back->setSurface(surf->egl);
    }

    this->surf = std::move(surf);
    reset_framebuffer();
    return true;
}

bool egl_output::reset_framebuffer()
{
    cleanup_framebuffer();

    auto const view_geo = out->viewGeometry();
    auto const centered_view = out->modeSize() != view_geo.size() || !view_geo.topLeft().isNull();

    if (out->transform() == output::Transform::Normal && !centered_view) {
        // No need to create intermediate framebuffer.
        return true;
    }

    // TODO(romangg): Also return in case wlroots can rotate in hardware.

    make_current();

    glGenFramebuffers(1, &render.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, render.framebuffer);
    GLRenderTarget::setKWinFramebuffer(render.framebuffer);

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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLRenderTarget::setKWinFramebuffer(0);

    return true;
}

bool egl_output::make_current() const
{
    return wlroots::make_current(surf->egl, egl_back);
}

bool egl_output::present(buffer* buf)
{
    auto drop_buffer = [buf] { wlr_buffer_drop(&buf->native.base); };
    auto render_output = static_cast<WaylandCompositor*>(Compositor::self())->outputs.at(out).get();

    render_output->swap_pending = true;
    wlr_output_attach_buffer(out->native, &buf->native.base);

    if (!wlr_output_test(out->native)) {
        qCWarning(KWIN_WL) << "Atomic output test failed on present.";
        reset(out);
        drop_buffer();
        return false;
    }
    if (!wlr_output_commit(out->native)) {
        qCWarning(KWIN_WL) << "Atomic output commit failed on present.";
        reset(out);
        drop_buffer();
        return false;
    }
    drop_buffer();
    return true;
}

}
