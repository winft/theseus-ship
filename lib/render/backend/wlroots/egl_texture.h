/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "texture_update.h"
#include "wlr_includes.h"

#include "render/gl/egl.h"
#include "render/gl/texture.h"

namespace KWin::render::backend::wlroots
{

template<typename Backend>
class egl_texture : public gl::texture_private<typename Backend::abstract_type>
{
public:
    using buffer_t = typename Backend::buffer_t;

    egl_texture(gl::texture<typename Backend::abstract_type>* texture, Backend* backend)
        : gl::texture_private<typename Backend::abstract_type>()
        , q(texture)
        , m_backend(backend)
    {
        this->m_target = GL_TEXTURE_2D;
        m_hasSubImageUnpack = hasGLExtension(QByteArrayLiteral("GL_EXT_unpack_subimage"));
    }

    ~egl_texture() override
    {
        if (m_image != EGL_NO_IMAGE_KHR) {
            eglDestroyImageKHR(m_backend->data.base.display, m_image);
        }
        wlr_texture_destroy(native);
    }

    bool updateTexture(buffer_t* buffer) override
    {
        return update_texture_from_buffer(*this, buffer);
    }

    Backend* backend() override
    {
        return m_backend;
    }

    gl::texture<typename Backend::abstract_type>* q;
    wlr_texture* native{nullptr};
    EGLImageKHR m_image{EGL_NO_IMAGE_KHR};
    bool m_hasSubImageUnpack{false};

    Backend* m_backend;
};

}
