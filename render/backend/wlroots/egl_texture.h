/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "texture_update.h"

#include "render/gl/egl.h"
#include "render/gl/texture.h"

#include <epoxy/egl.h>

namespace KWin::render::backend::wlroots
{

template<typename Backend>
class egl_texture : public gl::texture_private
{
public:
    egl_texture(gl::texture* texture, Backend* backend)
        : gl::texture_private()
        , q(texture)
        , m_backend(backend)
    {
        m_target = GL_TEXTURE_2D;
        m_hasSubImageUnpack = hasGLExtension(QByteArrayLiteral("GL_EXT_unpack_subimage"));
    }

    ~egl_texture() override
    {
        if (m_image != EGL_NO_IMAGE_KHR) {
            eglDestroyImageKHR(m_backend->data.base.display, m_image);
        }
    }

    bool updateTexture(render::buffer* buffer) override
    {
        return update_texture_from_buffer(*this, buffer);
    }

    gl::backend* backend() override
    {
        return m_backend;
    }

    gl::texture* q;
    EGLImageKHR m_image{EGL_NO_IMAGE_KHR};
    bool m_hasSubImageUnpack{false};

    Backend* m_backend;
};

}
