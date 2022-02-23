/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_dmabuf.h"
#include "kwin_eglext.h"
#include "kwinglplatform.h"
#include "toplevel.h"
#include "window.h"

#include <QImage>
#include <QOpenGLFramebufferObject>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>
#include <cassert>
#include <epoxy/gl.h>

namespace KWin::render::gl
{

// TODO(romangg): This function returns an image but has side effects on the texture. Either return
//                void or remove the side effects.
template<typename Texture>
EGLImageKHR attach_buffer_to_khr_image(Texture& texture, Wrapland::Server::Buffer* buffer)
{
    EGLint format, yInverted;
    texture.m_backend->data.query_wl_buffer(
        texture.m_backend->data.base.display, buffer->resource(), EGL_TEXTURE_FORMAT, &format);

    if (format != EGL_TEXTURE_RGB && format != EGL_TEXTURE_RGBA) {
        qCDebug(KWIN_WL) << "Unsupported texture format: " << format;
        return EGL_NO_IMAGE_KHR;
    }

    if (!texture.m_backend->data.query_wl_buffer(texture.m_backend->data.base.display,
                                                 buffer->resource(),
                                                 EGL_WAYLAND_Y_INVERTED_WL,
                                                 &yInverted)) {
        // if EGL_WAYLAND_Y_INVERTED_WL is not supported wl_buffer should be treated as if value
        // were EGL_TRUE
        yInverted = EGL_TRUE;
    }

    EGLint const attribs[] = {
        EGL_WAYLAND_PLANE_WL,
        0,
        EGL_NONE,
    };
    EGLImageKHR image = eglCreateImageKHR(texture.m_backend->data.base.display,
                                          EGL_NO_CONTEXT,
                                          EGL_WAYLAND_BUFFER_WL,
                                          (EGLClientBuffer)buffer->resource(),
                                          attribs);

    if (image != EGL_NO_IMAGE_KHR) {
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);
        texture.m_size = buffer->size();
        texture.updateMatrix();
        texture.q->setYInverted(yInverted);
    }

    return image;
}

template<typename Texture>
bool update_texture_from_image(Texture& texture, QImage const& image)
{
    if (image.isNull()) {
        return false;
    }

    glGenTextures(1, &texture.m_texture);
    texture.q->setFilter(GL_LINEAR);
    texture.q->setWrapMode(GL_CLAMP_TO_EDGE);

    auto const& size = image.size();
    texture.q->bind();

    GLenum format{0};
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
        if (Texture::s_supportsARGB32 && format == GL_RGBA8) {
            auto const im = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            glTexImage2D(texture.m_target,
                         0,
                         GL_BGRA_EXT,
                         im.width(),
                         im.height(),
                         0,
                         GL_BGRA_EXT,
                         GL_UNSIGNED_BYTE,
                         im.bits());
        } else {
            auto const im = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            glTexImage2D(texture.m_target,
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
        glTexImage2D(texture.m_target,
                     0,
                     format,
                     size.width(),
                     size.height(),
                     0,
                     GL_BGRA,
                     GL_UNSIGNED_BYTE,
                     image.bits());
    }

    texture.q->unbind();
    texture.q->setYInverted(true);
    texture.m_size = size;
    texture.updateMatrix();

    return true;
}

template<typename Texture>
bool update_texture_from_fbo(Texture& texture, const QSharedPointer<QOpenGLFramebufferObject>& fbo)
{
    if (fbo.isNull()) {
        return false;
    }

    texture.m_texture = fbo->texture();
    texture.m_size = fbo->size();

    texture.q->setWrapMode(GL_CLAMP_TO_EDGE);
    texture.q->setFilter(GL_LINEAR);
    texture.q->setYInverted(false);

    texture.updateMatrix();

    return true;
}

template<typename Texture>
bool update_texture_from_internal_image_object(Texture& texture, render::window_pixmap* pixmap)
{
    auto const image = pixmap->internalImage();
    if (image.isNull()) {
        return false;
    }

    if (texture.m_size != image.size()) {
        glDeleteTextures(1, &texture.m_texture);
        return load_internal_image_object(texture, pixmap);
    }

    texture_subimage_from_qimage(
        texture, image.devicePixelRatio(), image, pixmap->toplevel()->damage());

    return true;
}

template<typename Texture>
bool load_shm_texture(Texture& texture, Wrapland::Server::Buffer* buffer)
{
    return update_texture_from_image(texture, buffer->shmImage()->createQImage());
}

template<typename Texture>
bool load_internal_image_object(Texture& texture, render::window_pixmap* pixmap)
{
    return update_texture_from_image(texture, pixmap->internalImage());
}

template<typename Texture>
bool load_dmabuf_texture(Texture& texture, egl_dmabuf_buffer* dmabuf)
{
    assert(dmabuf);

    if (dmabuf->images().at(0) == EGL_NO_IMAGE_KHR) {
        qCritical(KWIN_WL) << "Invalid dmabuf-based wl_buffer";
        texture.q->discard();
        return false;
    }

    assert(texture.m_image == EGL_NO_IMAGE_KHR);

    glGenTextures(1, &texture.m_texture);
    texture.q->setWrapMode(GL_CLAMP_TO_EDGE);
    texture.q->setFilter(GL_NEAREST);

    texture.q->bind();
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)dmabuf->images().at(0));
    texture.q->unbind();

    texture.m_size = dmabuf->size();
    texture.q->setYInverted(!(dmabuf->flags() & Wrapland::Server::LinuxDmabufV1::YInverted));
    texture.updateMatrix();

    return true;
}

template<typename Texture>
bool load_egl_texture(Texture& texture, Wrapland::Server::Buffer* buffer)
{
    if (!texture.m_backend->data.query_wl_buffer) {
        return false;
    }
    if (!buffer->resource()) {
        return false;
    }

    glGenTextures(1, &texture.m_texture);
    texture.q->setWrapMode(GL_CLAMP_TO_EDGE);
    texture.q->setFilter(GL_LINEAR);

    texture.q->bind();
    texture.m_image = attach_buffer_to_khr_image(texture, buffer);
    texture.q->unbind();

    if (EGL_NO_IMAGE_KHR == texture.m_image) {
        qCDebug(KWIN_WL) << "failed to create egl image";
        texture.q->discard();
        return false;
    }

    return true;
}

template<typename Texture>
void texture_subimage(Texture& texture,
                      int scale,
                      Wrapland::Server::ShmImage const& img,
                      QRegion const& damage)
{
    auto prepareSubImage = [&](auto const& img, auto const& rect) {
        texture.q->bind();
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, img.stride() / (img.bpp() / 8));
        glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, rect.x());
        glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, rect.y());
    };
    auto finalizseSubImage = [&]() {
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
        texture.q->unbind();
    };
    auto getScaledRect = [scale](auto const& rect) {
        return QRect(
            rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
    };

    // Currently Wrapland only supports argb8888 and xrgb8888 formats, which both have the same Gl
    // counter-part. If more formats are added in the future this needs to be checked.
    auto const glFormat = GL_BGRA;

    if (GLPlatform::instance()->isGLES()) {
        if (Texture::s_supportsARGB32
            && (img.format() == Wrapland::Server::ShmImage::Format::argb8888)) {
            for (auto const& rect : damage) {
                auto const scaledRect = getScaledRect(rect);
                prepareSubImage(img, scaledRect);
                glTexSubImage2D(texture.m_target,
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
            for (auto const& rect : damage) {
                auto scaledRect = getScaledRect(rect);
                prepareSubImage(img, scaledRect);
                glTexSubImage2D(texture.m_target,
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
        for (auto const& rect : damage) {
            auto const scaledRect = getScaledRect(rect);
            prepareSubImage(img, scaledRect);
            glTexSubImage2D(texture.m_target,
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

template<typename Texture>
void texture_subimage_from_qimage(Texture& texture,
                                  int scale,
                                  QImage const& image,
                                  QRegion const& damage)
{
    texture.q->bind();

    if (GLPlatform::instance()->isGLES()) {
        if (Texture::s_supportsARGB32
            && (image.format() == QImage::Format_ARGB32
                || image.format() == QImage::Format_ARGB32_Premultiplied)) {
            auto const im = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            for (auto const& rect : damage) {
                auto scaledRect = QRect(rect.x() * scale,
                                        rect.y() * scale,
                                        rect.width() * scale,
                                        rect.height() * scale);
                glTexSubImage2D(texture.m_target,
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
            auto const im = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            for (auto const& rect : damage) {
                auto scaledRect = QRect(rect.x() * scale,
                                        rect.y() * scale,
                                        rect.width() * scale,
                                        rect.height() * scale);
                glTexSubImage2D(texture.m_target,
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
        auto const im = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (auto const& rect : damage) {
            auto scaledRect = QRect(
                rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
            glTexSubImage2D(texture.m_target,
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

    texture.q->unbind();
}

template<typename Texture>
bool load_texture_from_external(Texture& texture, render::window_pixmap* pixmap)
{
    auto buffer = pixmap->buffer();
    assert(buffer);

    if (auto surface = pixmap->surface()) {
        surface->resetTrackedDamage();
    }

    if (auto dmabuf = buffer->linuxDmabufBuffer()) {
        return load_dmabuf_texture(texture, static_cast<egl_dmabuf_buffer*>(dmabuf));
    }
    if (buffer->shmBuffer()) {
        return load_shm_texture(texture, buffer);
    }

    // As a last resort try loading via wl_drm.
    return load_egl_texture(texture, buffer);
}

template<typename Texture>
bool load_texture_from_internal(Texture& texture, render::window_pixmap* pixmap)
{
    assert(!pixmap->buffer());

    if (update_texture_from_fbo(texture, pixmap->fbo())) {
        return true;
    }
    return load_internal_image_object(texture, pixmap);
}

template<typename Texture>
bool load_texture_from_pixmap(Texture& texture, render::window_pixmap* pixmap)
{
    if (pixmap->buffer()) {
        return load_texture_from_external(texture, pixmap);
    }
    return load_texture_from_internal(texture, pixmap);
}

template<typename Texture>
void update_texture_from_pixmap(Texture& texture, render::window_pixmap* pixmap)
{
    // FIXME: Refactor this method.

    auto const buffer = pixmap->buffer();
    if (!buffer) {
        if (update_texture_from_fbo(texture, pixmap->fbo())) {
            return;
        }
        if (update_texture_from_internal_image_object(texture, pixmap)) {
            return;
        }
        return;
    }

    auto s = pixmap->surface();
    if (auto dmabuf = static_cast<egl_dmabuf_buffer*>(buffer->linuxDmabufBuffer())) {
        if (dmabuf->images().size() == 0) {
            return;
        }

        texture.q->bind();

        // TODO
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)dmabuf->images().at(0));
        texture.q->unbind();

        if (texture.m_image != EGL_NO_IMAGE_KHR) {
            eglDestroyImageKHR(texture.m_backend->data.base.display, texture.m_image);
        }

        // The wl_buffer has ownership of the image
        texture.m_image = EGL_NO_IMAGE_KHR;

        // The origin in a dmabuf-buffer is at the upper-left corner, so the meaning
        // of Y-inverted is the inverse of OpenGL.
        texture.q->setYInverted(!(dmabuf->flags() & Wrapland::Server::LinuxDmabufV1::YInverted));

        if (s) {
            s->resetTrackedDamage();
        }

        return;
    }

    if (!buffer->shmBuffer()) {
        texture.q->bind();
        auto image = attach_buffer_to_khr_image(texture, buffer);
        texture.q->unbind();

        if (image != EGL_NO_IMAGE_KHR) {
            if (texture.m_image != EGL_NO_IMAGE_KHR) {
                eglDestroyImageKHR(texture.m_backend->data.base.display, texture.m_image);
            }
            texture.m_image = image;
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

    if (buffer->size() != texture.m_size) {
        // buffer size has changed, reload shm texture
        if (!load_texture_from_pixmap(texture, pixmap)) {
            return;
        }
    }

    assert(buffer->size() == texture.m_size);

    auto const damage = s->trackedDamage();
    s->resetTrackedDamage();

    if (!GLPlatform::instance()->isGLES() || texture.m_hasSubImageUnpack) {
        texture_subimage(texture, s->state().scale, shmImage.value(), damage);
    } else {
        texture_subimage_from_qimage(texture, s->state().scale, shmImage->createQImage(), damage);
    }
}

}
