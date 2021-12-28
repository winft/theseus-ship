/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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

#include "backend.h"
#include "texture.h"

#include "render/wayland/egl_data.h"

class QOpenGLFramebufferObject;

namespace Wrapland
{
namespace Server
{
class Buffer;
class ShmImage;
}
}

namespace KWin::render::gl
{

class egl_dmabuf;

class KWIN_EXPORT egl_backend : public backend
{
public:
    ~egl_backend() override;

    void cleanup();
    virtual void cleanupSurfaces();

    bool makeCurrent() override;
    void doneCurrent() override;
    render::gl::texture_private* createBackendTexture(render::gl::texture* texture) override;

    bool hasClientExtension(const QByteArray& ext) const;

    egl_dmabuf* dmabuf{nullptr};
    wayland::egl_data data;
};

class KWIN_EXPORT egl_texture : public render::gl::texture_private
{
public:
    egl_texture(render::gl::texture* texture, egl_backend* backend);
    ~egl_texture() override;
    bool loadTexture(render::window_pixmap* pixmap) override;
    void updateTexture(render::window_pixmap* pixmap) override;
    render::gl::backend* backend() override;

    render::gl::texture* q;
    EGLImageKHR m_image{EGL_NO_IMAGE_KHR};
    bool m_hasSubImageUnpack{false};

    egl_backend* m_backend;

protected:
    EGLImageKHR image() const
    {
        return m_image;
    }
    void setImage(const EGLImageKHR& img)
    {
        m_image = img;
    }
    render::gl::texture* texture() const
    {
        return q;
    }
};

}
