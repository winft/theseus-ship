/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/gl/texture.h"

#include <epoxy/egl.h>

namespace KWin::render::backend::wlroots
{

class egl_backend;

class egl_texture : public gl::texture_private
{
public:
    egl_texture(gl::texture* texture, egl_backend* backend);
    ~egl_texture() override;

    bool updateTexture(window_pixmap* pixmap) override;
    gl::backend* backend() override;

    gl::texture* q;
    EGLImageKHR m_image{EGL_NO_IMAGE_KHR};
    bool m_hasSubImageUnpack{false};

    egl_backend* m_backend;
};

}
