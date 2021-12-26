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
#include "kwin_eglext.h"
#include "texture.h"

#include "options.h"
#include "render/compositor.h"
#include "render/platform.h"
#include "render/wayland/egl.h"
#include "render/window.h"
#include "toplevel.h"
#include "wayland_server.h"

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/surface.h>

#include <kwinglplatform.h>
#include <kwinglutils.h>
#include <wayland_logging.h>

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

#include <memory>

namespace KWin::render::gl
{

egl_backend::egl_backend()
    : QObject(nullptr)
    , backend()
{
    connect(render::compositor::self(), &render::compositor::aboutToDestroy, this, [this] {
        wayland::unbind_egl_display(*this, data);
    });
}

egl_backend::~egl_backend()
{
    delete dmabuf;
}

void egl_backend::cleanup()
{
    cleanupGL();
    doneCurrent();
    eglDestroyContext(data.base.display, data.base.context);
    cleanupSurfaces();
    eglReleaseThread();
}

void egl_backend::cleanupSurfaces()
{
    if (data.base.surface != EGL_NO_SURFACE) {
        eglDestroySurface(data.base.display, data.base.surface);
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
    , m_image(EGL_NO_IMAGE_KHR)
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
    // FIXME: Refactor this method.

    const auto& buffer = pixmap->buffer();
    if (!buffer) {
        if (updateFromFBO(pixmap->fbo())) {
            return true;
        }
        if (loadInternalImageObject(pixmap)) {
            return true;
        }
        return false;
    }
    // try Wayland loading
    if (auto s = pixmap->surface()) {
        s->resetTrackedDamage();
    }
    if (buffer->linuxDmabufBuffer()) {
        return loadDmabufTexture(buffer);
    } else if (buffer->shmBuffer()) {
        return loadShmTexture(buffer);
    }
    return loadEglTexture(buffer);
}

void egl_texture::updateTexture(render::window_pixmap* pixmap)
{
    // FIXME: Refactor this method.

    auto const buffer = pixmap->buffer();
    if (!buffer) {
        if (updateFromFBO(pixmap->fbo())) {
            return;
        }
        if (updateFromInternalImageObject(pixmap)) {
            return;
        }
        return;
    }
    auto s = pixmap->surface();
    if (auto dmabuf = static_cast<egl_dmabuf_buffer*>(buffer->linuxDmabufBuffer())) {
        if (dmabuf->images().size() == 0) {
            return;
        }
        q->bind();
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)dmabuf->images().at(0)); // TODO
        q->unbind();
        if (m_image != EGL_NO_IMAGE_KHR) {
            eglDestroyImageKHR(m_backend->data.base.display, m_image);
        }
        m_image = EGL_NO_IMAGE_KHR; // The wl_buffer has ownership of the image
        // The origin in a dmabuf-buffer is at the upper-left corner, so the meaning
        // of Y-inverted is the inverse of OpenGL.
        q->setYInverted(!(dmabuf->flags() & Wrapland::Server::LinuxDmabufV1::YInverted));
        if (s) {
            s->resetTrackedDamage();
        }
        return;
    }
    if (!buffer->shmBuffer()) {
        q->bind();
        EGLImageKHR image = attach(buffer);
        q->unbind();
        if (image != EGL_NO_IMAGE_KHR) {
            if (m_image != EGL_NO_IMAGE_KHR) {
                eglDestroyImageKHR(m_backend->data.base.display, m_image);
            }
            m_image = image;
        }
        if (s) {
            s->resetTrackedDamage();
        }
        return;
    }
    // shm fallback
    auto shmImage = buffer->shmImage();
    if (!shmImage || !s) {
        return;
    }
    if (buffer->size() != m_size) {
        // buffer size has changed, reload shm texture
        if (!loadTexture(pixmap)) {
            return;
        }
    }
    Q_ASSERT(buffer->size() == m_size);
    const QRegion damage = s->trackedDamage();
    s->resetTrackedDamage();

    if (!GLPlatform::instance()->isGLES() || m_hasSubImageUnpack) {
        textureSubImage(s->state().scale, shmImage.value(), damage);
    } else {
        textureSubImageFromQImage(s->state().scale, shmImage->createQImage(), damage);
    }
}

bool egl_texture::createTextureImage(const QImage& image)
{
    if (image.isNull()) {
        return false;
    }

    glGenTextures(1, &m_texture);
    q->setFilter(GL_LINEAR);
    q->setWrapMode(GL_CLAMP_TO_EDGE);

    const QSize& size = image.size();
    q->bind();
    GLenum format = 0;
    switch (image.format()) {
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
        format = GL_RGBA8;
        break;
    case QImage::Format_RGB32:
        format = GL_RGB8;
        break;
    default:
        return false;
    }
    if (GLPlatform::instance()->isGLES()) {
        if (s_supportsARGB32 && format == GL_RGBA8) {
            const QImage im = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            glTexImage2D(m_target,
                         0,
                         GL_BGRA_EXT,
                         im.width(),
                         im.height(),
                         0,
                         GL_BGRA_EXT,
                         GL_UNSIGNED_BYTE,
                         im.bits());
        } else {
            const QImage im = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            glTexImage2D(m_target,
                         0,
                         GL_RGBA,
                         im.width(),
                         im.height(),
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         im.bits());
        }
    } else {
        glTexImage2D(m_target,
                     0,
                     format,
                     size.width(),
                     size.height(),
                     0,
                     GL_BGRA,
                     GL_UNSIGNED_BYTE,
                     image.bits());
    }
    q->unbind();
    q->setYInverted(true);
    m_size = size;
    updateMatrix();
    return true;
}

void egl_texture::textureSubImage(int scale,
                                  Wrapland::Server::ShmImage const& img,
                                  const QRegion& damage)
{
    auto prepareSubImage = [this](Wrapland::Server::ShmImage const& img, QRect const& rect) {
        q->bind();
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, img.stride() / (img.bpp() / 8));
        glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, rect.x());
        glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, rect.y());
    };
    auto finalizseSubImage = [this]() {
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
        q->unbind();
    };
    auto getScaledRect = [scale](QRect const& rect) {
        return QRect(
            rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
    };

    // Currently Wrapland only supports argb8888 and xrgb8888 formats, which both have the same Gl
    // counter-part. If more formats are added in the future this needs to be checked.
    auto const glFormat = GL_BGRA;

    if (GLPlatform::instance()->isGLES()) {
        if (s_supportsARGB32 && (img.format() == Wrapland::Server::ShmImage::Format::argb8888)) {
            for (const QRect& rect : damage) {
                auto const scaledRect = getScaledRect(rect);
                prepareSubImage(img, scaledRect);
                glTexSubImage2D(m_target,
                                0,
                                scaledRect.x(),
                                scaledRect.y(),
                                scaledRect.width(),
                                scaledRect.height(),
                                glFormat,
                                GL_UNSIGNED_BYTE,
                                img.data());
                finalizseSubImage();
            }
        } else {
            for (const QRect& rect : damage) {
                auto scaledRect = getScaledRect(rect);
                prepareSubImage(img, scaledRect);
                glTexSubImage2D(m_target,
                                0,
                                scaledRect.x(),
                                scaledRect.y(),
                                scaledRect.width(),
                                scaledRect.height(),
                                glFormat,
                                GL_UNSIGNED_BYTE,
                                img.data());
                finalizseSubImage();
            }
        }
    } else {
        for (const QRect& rect : damage) {
            auto const scaledRect = getScaledRect(rect);
            prepareSubImage(img, scaledRect);
            glTexSubImage2D(m_target,
                            0,
                            scaledRect.x(),
                            scaledRect.y(),
                            scaledRect.width(),
                            scaledRect.height(),
                            glFormat,
                            GL_UNSIGNED_BYTE,
                            img.data());
            finalizseSubImage();
        }
    }
}

void egl_texture::textureSubImageFromQImage(int scale, const QImage& image, const QRegion& damage)
{
    q->bind();
    if (GLPlatform::instance()->isGLES()) {
        if (s_supportsARGB32
            && (image.format() == QImage::Format_ARGB32
                || image.format() == QImage::Format_ARGB32_Premultiplied)) {
            const QImage im = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            for (const QRect& rect : damage) {
                auto scaledRect = QRect(rect.x() * scale,
                                        rect.y() * scale,
                                        rect.width() * scale,
                                        rect.height() * scale);
                glTexSubImage2D(m_target,
                                0,
                                scaledRect.x(),
                                scaledRect.y(),
                                scaledRect.width(),
                                scaledRect.height(),
                                GL_BGRA_EXT,
                                GL_UNSIGNED_BYTE,
                                im.copy(scaledRect).constBits());
            }
        } else {
            const QImage im = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            for (const QRect& rect : damage) {
                auto scaledRect = QRect(rect.x() * scale,
                                        rect.y() * scale,
                                        rect.width() * scale,
                                        rect.height() * scale);
                glTexSubImage2D(m_target,
                                0,
                                scaledRect.x(),
                                scaledRect.y(),
                                scaledRect.width(),
                                scaledRect.height(),
                                GL_RGBA,
                                GL_UNSIGNED_BYTE,
                                im.copy(scaledRect).constBits());
            }
        }
    } else {
        const QImage im = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (const QRect& rect : damage) {
            auto scaledRect = QRect(
                rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
            glTexSubImage2D(m_target,
                            0,
                            scaledRect.x(),
                            scaledRect.y(),
                            scaledRect.width(),
                            scaledRect.height(),
                            GL_BGRA,
                            GL_UNSIGNED_BYTE,
                            im.copy(scaledRect).constBits());
        }
    }
    q->unbind();
}

bool egl_texture::loadShmTexture(Wrapland::Server::Buffer* buffer)
{
    return createTextureImage(buffer->shmImage()->createQImage());
}

bool egl_texture::loadEglTexture(Wrapland::Server::Buffer* buffer)
{
    if (!m_backend->data.query_wl_buffer) {
        return false;
    }
    if (!buffer->resource()) {
        return false;
    }

    glGenTextures(1, &m_texture);
    q->setWrapMode(GL_CLAMP_TO_EDGE);
    q->setFilter(GL_LINEAR);
    q->bind();
    m_image = attach(buffer);
    q->unbind();

    if (EGL_NO_IMAGE_KHR == m_image) {
        qCDebug(KWIN_WL) << "failed to create egl image";
        q->discard();
        return false;
    }

    return true;
}

bool egl_texture::loadDmabufTexture(Wrapland::Server::Buffer* buffer)
{
    auto* dmabuf = static_cast<egl_dmabuf_buffer*>(buffer->linuxDmabufBuffer());
    if (!dmabuf || dmabuf->images().at(0) == EGL_NO_IMAGE_KHR) {
        qCritical(KWIN_WL) << "Invalid dmabuf-based wl_buffer";
        q->discard();
        return false;
    }

    Q_ASSERT(m_image == EGL_NO_IMAGE_KHR);

    glGenTextures(1, &m_texture);
    q->setWrapMode(GL_CLAMP_TO_EDGE);
    q->setFilter(GL_NEAREST);
    q->bind();
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)dmabuf->images().at(0));
    q->unbind();

    m_size = dmabuf->size();
    q->setYInverted(!(dmabuf->flags() & Wrapland::Server::LinuxDmabufV1::YInverted));
    updateMatrix();

    return true;
}

bool egl_texture::loadInternalImageObject(render::window_pixmap* pixmap)
{
    return createTextureImage(pixmap->internalImage());
}

EGLImageKHR egl_texture::attach(Wrapland::Server::Buffer* buffer)
{
    EGLint format, yInverted;
    m_backend->data.query_wl_buffer(
        m_backend->data.base.display, buffer->resource(), EGL_TEXTURE_FORMAT, &format);
    if (format != EGL_TEXTURE_RGB && format != EGL_TEXTURE_RGBA) {
        qCDebug(KWIN_WL) << "Unsupported texture format: " << format;
        return EGL_NO_IMAGE_KHR;
    }
    if (!m_backend->data.query_wl_buffer(m_backend->data.base.display,
                                         buffer->resource(),
                                         EGL_WAYLAND_Y_INVERTED_WL,
                                         &yInverted)) {
        // if EGL_WAYLAND_Y_INVERTED_WL is not supported wl_buffer should be treated as if value
        // were EGL_TRUE
        yInverted = EGL_TRUE;
    }

    const EGLint attribs[] = {EGL_WAYLAND_PLANE_WL, 0, EGL_NONE};
    EGLImageKHR image = eglCreateImageKHR(m_backend->data.base.display,
                                          EGL_NO_CONTEXT,
                                          EGL_WAYLAND_BUFFER_WL,
                                          (EGLClientBuffer)buffer->resource(),
                                          attribs);
    if (image != EGL_NO_IMAGE_KHR) {
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);
        m_size = buffer->size();
        updateMatrix();
        q->setYInverted(yInverted);
    }
    return image;
}

bool egl_texture::updateFromFBO(const QSharedPointer<QOpenGLFramebufferObject>& fbo)
{
    if (fbo.isNull()) {
        return false;
    }
    m_texture = fbo->texture();
    m_size = fbo->size();
    q->setWrapMode(GL_CLAMP_TO_EDGE);
    q->setFilter(GL_LINEAR);
    q->setYInverted(false);
    updateMatrix();
    return true;
}

bool egl_texture::updateFromInternalImageObject(render::window_pixmap* pixmap)
{
    const QImage image = pixmap->internalImage();
    if (image.isNull()) {
        return false;
    }

    if (m_size != image.size()) {
        glDeleteTextures(1, &m_texture);
        return loadInternalImageObject(pixmap);
    }

    textureSubImageFromQImage(image.devicePixelRatio(), image, pixmap->toplevel()->damage());

    return true;
}

}
