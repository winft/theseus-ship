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
#pragma once

#include <kwingl/texture_p.h>
#include <kwingl/utils.h>

namespace KWin::render::gl
{

template<typename Backend>
class texture_private : public GLTexturePrivate
{
public:
    virtual bool updateTexture(typename Backend::buffer_t* buffer) = 0;
    virtual Backend* backend() = 0;
};

template<typename Backend>
class texture : public GLTexture
{
public:
    using buffer_t = typename Backend::buffer_t;
    using private_t = texture_private<Backend>;

    explicit texture(Backend* backend)
        : GLTexture(*backend->createBackendTexture(this))
    {
    }

    texture& operator=(texture const& tex)
    {
        d_ptr = tex.d_ptr;
        return *this;
    }

    void discard() override final
    {
        d_ptr = d_func()->backend()->createBackendTexture(this);
    }

    bool load(buffer_t* buffer)
    {
        if (!buffer->isValid()) {
            return false;
        }

        // decrease the reference counter for the old texture
        // new texture_private();
        d_ptr = d_func()->backend()->createBackendTexture(this);

        return d_func()->updateTexture(buffer);
    }

    void update_from_buffer(buffer_t* buffer)
    {
        d_func()->updateTexture(buffer);
    }

    inline private_t* d_func()
    {
        return reinterpret_cast<private_t*>(qGetPtrHelper(d_ptr));
    }
    inline const private_t* d_func() const
    {
        return reinterpret_cast<private_t const*>(qGetPtrHelper(d_ptr));
    }
};

}
