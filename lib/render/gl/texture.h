/*
SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
