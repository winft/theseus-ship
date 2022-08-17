/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "buffer.h"

#include "render/wayland/buffer.h"

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>

namespace KWin::render::qpainter
{

buffer::buffer(render::window* window)
    : render::buffer(window)
{
}

buffer::~buffer()
{
}

void buffer::create()
{
    if (isValid()) {
        return;
    }
    render::buffer::create();
    if (!isValid()) {
        return;
    }

    // For now we rely on the fact that the QPainter backend is only run on Wayland.
    auto& win_integrate = static_cast<render::wayland::buffer_win_integration&>(*win_integration);

    if (!window->ref_win->surface) {
        // That's an internal client.
        m_image = win_integrate.internal.image;
        return;
    }

    // performing deep copy, this could probably be improved
    m_image = win_integrate.external->shmImage()->createQImage().copy();
    if (auto s = window->ref_win->surface) {
        s->resetTrackedDamage();
    }
}

bool buffer::isValid() const
{
    if (!m_image.isNull()) {
        return true;
    }
    return render::buffer::isValid();
}

void buffer::updateBuffer()
{
    // For now we rely on the fact that the QPainter backend is only run on Wayland.
    auto& win_integrate = static_cast<render::wayland::buffer_win_integration&>(*win_integration);

    auto oldBuffer = win_integrate.external.get();
    render::buffer::updateBuffer();
    auto b = win_integrate.external.get();

    if (!window->ref_win->surface) {
        // That's an internal client.
        m_image = win_integrate.internal.image;
        return;
    }
    if (!b) {
        m_image = QImage();
        return;
    }
    if (b == oldBuffer) {
        return;
    }

    // perform deep copy
    m_image = b->shmImage()->createQImage().copy();
    if (auto surface = window->ref_win->surface) {
        surface->resetTrackedDamage();
    }
}

QImage const& buffer::image()
{
    return m_image;
}

}
