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
#include "egl_backend.h"

#include "egl.h"
#include "egl_dmabuf.h"
#include "egl_texture.h"
#include "kwin_eglext.h"
#include "texture.h"

#include "options.h"
#include "render/compositor.h"
#include "render/platform.h"
#include "render/window.h"

#include <Wrapland/Server/buffer.h>

#include <kwinglplatform.h>
#include <kwinglutils.h>

#include <QOpenGLContext>

namespace KWin::render::gl
{

egl_backend::~egl_backend() = default;

void egl_backend::cleanup()
{
    cleanupGL();
    doneCurrent();
    eglDestroyContext(data.base.display, data.base.context);
    cleanupSurfaces();
    eglReleaseThread();

    delete dmabuf;
    dmabuf = nullptr;
}

void egl_backend::cleanupSurfaces()
{
    if (data.base.surface != EGL_NO_SURFACE) {
        eglDestroySurface(data.base.display, data.base.surface);
        data.base.surface = EGL_NO_SURFACE;
    }
}

bool egl_backend::hasClientExtension(const QByteArray& ext) const
{
    return data.base.client_extensions.contains(ext);
}

bool egl_backend::makeCurrent()
{
    if (QOpenGLContext* context = QOpenGLContext::currentContext()) {
        // Workaround to tell Qt that no QOpenGLContext is current
        context->doneCurrent();
    }
    const bool current = eglMakeCurrent(
        data.base.display, data.base.surface, data.base.surface, data.base.context);
    return current;
}

void egl_backend::doneCurrent()
{
    eglMakeCurrent(data.base.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

render::gl::texture_private* egl_backend::createBackendTexture(render::gl::texture* texture)
{
    return new egl_texture(texture, this);
}

egl_texture::egl_texture(render::gl::texture* texture, egl_backend* backend)
    : render::gl::texture_private()
    , q(texture)
    , m_backend(backend)
{
    m_target = GL_TEXTURE_2D;
    m_hasSubImageUnpack = hasGLExtension(QByteArrayLiteral("GL_EXT_unpack_subimage"));
}

egl_texture::~egl_texture()
{
    if (m_image != EGL_NO_IMAGE_KHR) {
        eglDestroyImageKHR(m_backend->data.base.display, m_image);
    }
}

render::gl::backend* egl_texture::backend()
{
    return m_backend;
}

bool egl_texture::loadTexture(render::window_pixmap* pixmap)
{
    return load_texture_from_pixmap(*this, pixmap);
}

void egl_texture::updateTexture(render::window_pixmap* pixmap)
{
    update_texture_from_pixmap(*this, pixmap);
}

}
