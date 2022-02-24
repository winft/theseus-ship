/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglplatform.h"
#include "render/gl/egl_dmabuf.h"
#include "render/gl/kwin_eglext.h"
#include "render/gl/window.h"
#include "toplevel.h"
#include "wayland_logging.h"

#include <QImage>
#include <QOpenGLFramebufferObject>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>
#include <cassert>
#include <epoxy/gl.h>

namespace KWin::render::backend::wlroots
{

template<typename Texture>
void attach_buffer_to_khr_image(Texture& texture, Wrapland::Server::Buffer* buffer)
{
    auto const& egl_data = texture.m_backend->data;
    EGLint format, yInverted;

    egl_data.query_wl_buffer(
        egl_data.base.display, buffer->resource(), EGL_TEXTURE_FORMAT, &format);

    if (format != EGL_TEXTURE_RGB && format != EGL_TEXTURE_RGBA) {
        qCDebug(KWIN_WL) << "Unsupported texture format: " << format;
        return;
    }

    if (!egl_data.query_wl_buffer(
            egl_data.base.display, buffer->resource(), EGL_WAYLAND_Y_INVERTED_WL, &yInverted)) {
        // if EGL_WAYLAND_Y_INVERTED_WL is not supported wl_buffer should be treated as if value
        // were EGL_TRUE
        yInverted = EGL_TRUE;
    }

    EGLint const attribs[] = {
        EGL_WAYLAND_PLANE_WL,
        0,
        EGL_NONE,
    };
    EGLImageKHR image = eglCreateImageKHR(egl_data.base.display,
                                          EGL_NO_CONTEXT,
                                          EGL_WAYLAND_BUFFER_WL,
                                          (EGLClientBuffer)buffer->resource(),
                                          attribs);
    if (image == EGL_NO_IMAGE_KHR) {
        return;
    }

    texture.q->bind();
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);
    texture.q->unbind();

    if (texture.m_image != EGL_NO_IMAGE_KHR) {
        eglDestroyImageKHR(egl_data.base.display, texture.m_image);
    }
    texture.m_image = image;

    texture.m_size = buffer->size();
    texture.updateMatrix();
    texture.q->setYInverted(yInverted);
}

template<typename Texture>
bool load_texture_from_image(Texture& texture, QImage const& image)
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
bool update_texture_from_internal_image_object(Texture& texture, window_pixmap* pixmap)
{
    auto const image = pixmap->internalImage();
    if (image.isNull()) {
        return false;
    }

    if (texture.m_size != image.size()) {
        glDeleteTextures(1, &texture.m_texture);
        return load_texture_from_image(texture, image);
    }

    texture_subimage_from_qimage(
        texture, image.devicePixelRatio(), image, pixmap->toplevel()->damage());

    return true;
}

template<typename Texture>
bool update_texture_from_egl(Texture& texture, Wrapland::Server::Buffer* buffer)
{
    if (!texture.m_texture) {
        if (!texture.m_backend->data.query_wl_buffer) {
            return false;
        }
        if (!buffer->resource()) {
            // TODO(romangg): can we assert instead?
            return false;
        }

        glGenTextures(1, &texture.m_texture);
        texture.q->setWrapMode(GL_CLAMP_TO_EDGE);
        texture.q->setFilter(GL_LINEAR);
    }

    attach_buffer_to_khr_image(texture, buffer);

    if (texture.m_image == EGL_NO_IMAGE_KHR) {
        qCDebug(KWIN_WL) << "Failed to update texture via EGL/wl_drm";
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
}

template<typename Texture>
void texture_subimage_from_qimage(Texture& texture,
                                  int scale,
                                  QImage const& image,
                                  QRegion const& damage)
{
    texture.q->bind();

    if (Texture::s_supportsARGB32
        && (image.format() == QImage::Format_ARGB32
            || image.format() == QImage::Format_ARGB32_Premultiplied)) {
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
                            GL_BGRA_EXT,
                            GL_UNSIGNED_BYTE,
                            im.copy(scaledRect).constBits());
        }
    } else {
        auto const im = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        for (auto const& rect : damage) {
            auto scaledRect = QRect(
                rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
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

    texture.q->unbind();
}

template<typename Texture>
bool update_texture_from_dmabuf(Texture& texture, gl::egl_dmabuf_buffer* dmabuf)
{
    assert(dmabuf);
    assert(texture.m_image == EGL_NO_IMAGE_KHR);

    if (dmabuf->images().empty() || dmabuf->images().at(0) == EGL_NO_IMAGE_KHR) {
        qCritical(KWIN_WL) << "Invalid dmabuf-based wl_buffer";
        texture.q->discard();
        return false;
    }

    texture.q->bind();

    if (!texture.m_texture) {
        // Recreate the texture.
        glGenTextures(1, &texture.m_texture);

        texture.q->setWrapMode(GL_CLAMP_TO_EDGE);
        texture.q->setFilter(GL_NEAREST);
    }

    // TODO
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)dmabuf->images().at(0));
    texture.q->unbind();

    if (texture.m_size != dmabuf->size()) {
        texture.m_size = dmabuf->size();
        texture.updateMatrix();
    }

    // The origin in a dmabuf-buffer is at the upper-left corner, so the meaning
    // of Y-inverted is the inverse of OpenGL.
    texture.q->setYInverted(!(dmabuf->flags() & Wrapland::Server::LinuxDmabufV1::YInverted));

    return true;
}

template<typename Texture>
bool update_texture_from_shm(Texture& texture, window_pixmap* pixmap)
{
    auto const buffer = pixmap->buffer();
    assert(buffer && buffer->shmBuffer());

    auto image = buffer->shmImage();
    auto surface = pixmap->surface();
    if (!image || !surface) {
        return false;
    }

    if (buffer->size() != texture.m_size) {
        // First time update or buffer size has changed.
        return load_texture_from_image(texture, image->createQImage());
    }

    assert(buffer->size() == texture.m_size);
    auto const& damage = surface->trackedDamage();

    if (texture.m_hasSubImageUnpack) {
        texture_subimage(texture, surface->state().scale, image.value(), damage);
    } else {
        texture_subimage_from_qimage(
            texture, surface->state().scale, image->createQImage(), damage);
    }

    return true;
}

template<typename Texture>
bool update_texture_from_external(Texture& texture, window_pixmap* pixmap)
{
    bool ret;
    auto const buffer = pixmap->buffer();
    assert(buffer);

    if (auto dmabuf = buffer->linuxDmabufBuffer()) {
        ret = update_texture_from_dmabuf(texture, static_cast<gl::egl_dmabuf_buffer*>(dmabuf));
    } else if (auto shm = buffer->shmBuffer()) {
        ret = update_texture_from_shm(texture, pixmap);
    } else {
        ret = update_texture_from_egl(texture, buffer);
    }

    if (auto surface = pixmap->surface()) {
        surface->resetTrackedDamage();
    }

    return ret;
}

template<typename Texture>
bool update_texture_from_internal(Texture& texture, window_pixmap* pixmap)
{
    assert(!pixmap->buffer());

    return update_texture_from_fbo(texture, pixmap->fbo())
        || update_texture_from_internal_image_object(texture, pixmap);
}

template<typename Texture>
bool update_texture_from_pixmap(Texture& texture, window_pixmap* pixmap)
{
    if (pixmap->buffer()) {
        return update_texture_from_external(texture, pixmap);
    }
    return update_texture_from_internal(texture, pixmap);
}

}
