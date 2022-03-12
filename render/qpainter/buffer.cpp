/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "buffer.h"

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
    if (!surface()) {
        // That's an internal client.
        m_image = internalImage();
        return;
    }
    // performing deep copy, this could probably be improved
    m_image = wayland_buffer()->shmImage()->createQImage().copy();
    if (auto s = surface()) {
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
    auto oldBuffer = wayland_buffer();
    render::buffer::updateBuffer();
    auto b = wayland_buffer();
    if (!surface()) {
        // That's an internal client.
        m_image = internalImage();
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
    if (auto s = surface()) {
        s->resetTrackedDamage();
    }
}

QImage const& buffer::image()
{
    return m_image;
}

}
