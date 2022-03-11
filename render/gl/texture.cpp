/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "texture.h"

#include "backend.h"
#include "window.h"

namespace KWin::render::gl
{

texture::texture(gl::backend* backend)
    : GLTexture(*backend->createBackendTexture(this))
{
}

texture::~texture()
{
}

texture& texture::operator=(texture const& tex)
{
    d_ptr = tex.d_ptr;
    return *this;
}

void texture::discard()
{
    d_ptr = d_func()->backend()->createBackendTexture(this);
}

bool texture::load(render::buffer* buffer)
{
    if (!buffer->isValid()) {
        return false;
    }

    // decrease the reference counter for the old texture
    // new texture_private();
    d_ptr = d_func()->backend()->createBackendTexture(this);

    return d_func()->updateTexture(buffer);
}

void texture::update_from_buffer(render::buffer* buffer)
{
    d_func()->updateTexture(buffer);
}

texture_private::texture_private()
{
}

texture_private::~texture_private()
{
}

}
