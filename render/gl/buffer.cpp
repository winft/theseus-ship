/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "buffer.h"

#include "scene.h"
#include "texture.h"

#include "base/logging.h"
#include "toplevel.h"

#include <Wrapland/Server/surface.h>

namespace KWin::render::gl
{

buffer::buffer(render::window* window, gl::scene* scene)
    : render::buffer(window)
    , m_texture(scene->createTexture())
    , m_scene(scene)
{
}

buffer::~buffer()
{
}

static bool needs_buffer_update(gl::buffer const* buffer)
{
    // That's a regular Wayland client.
    if (buffer->surface()) {
        return !buffer->surface()->trackedDamage().isEmpty();
    }

    // That's an internal client with a raster buffer attached.
    if (!buffer->internalImage().isNull()) {
        return !buffer->toplevel()->damage().isEmpty();
    }

    // That's an internal client with an opengl framebuffer object attached.
    if (buffer->fbo()) {
        return !buffer->toplevel()->damage().isEmpty();
    }

    // That's an X11 client.
    return false;
}

render::gl::texture* buffer::texture() const
{
    return m_texture.data();
}

bool buffer::bind()
{
    if (!m_texture->isNull()) {
        if (!toplevel()->damage().isEmpty()) {
            updateBuffer();
        }
        if (needs_buffer_update(this)) {
            m_texture->update_from_buffer(this);
            // mipmaps need to be updated
            m_texture->setDirty();
        }
        toplevel()->resetDamage();
        return true;
    }
    if (!isValid()) {
        return false;
    }

    bool success = m_texture->load(this);

    if (success) {
        toplevel()->resetDamage();
    } else {
        qCDebug(KWIN_CORE) << "Failed to bind window";
    }
    return success;
}

bool buffer::isValid() const
{
    if (!m_texture->isNull()) {
        return true;
    }
    return render::buffer::isValid();
}

}
