/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "platform.h"
#include "wlr_helpers.h"
#include "wlr_includes.h"
#include "wlr_non_owning_data_buffer.h"

#include "render/gl/window.h"
#include "render/wayland/buffer.h"

#include <render/gl/interface/platform.h>

#include <QImage>
#include <QOpenGLFramebufferObject>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/linux_dmabuf_v1.h>
#include <Wrapland/Server/surface.h>
#include <cassert>
#include <drm_fourcc.h>
#include <epoxy/gl.h>

#ifndef EGL_WL_bind_wayland_display
#define EGL_WAYLAND_BUFFER_WL 0x31D5
#define EGL_WAYLAND_PLANE_WL 0x31D6
#define EGL_WAYLAND_Y_INVERTED_WL 0x31DB
#endif

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
        qCDebug(KWIN_CORE) << "Unsupported texture format: " << format;
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
    texture.q->set_content_transform(yInverted ? effect::transform_type::flipped_180
                                               : effect::transform_type::normal);
}

template<typename Texture>
bool update_texture_from_fbo(Texture& texture, std::shared_ptr<QOpenGLFramebufferObject> const& fbo)
{
    if (!fbo) {
        return false;
    }

    texture.m_texture = fbo->texture();
    texture.m_size = fbo->size();

    texture.q->setWrapMode(GL_CLAMP_TO_EDGE);
    texture.q->setFilter(GL_LINEAR);
    texture.q->set_content_transform(effect::transform_type::normal);

    texture.updateMatrix();

    return true;
}

template<typename Texture, typename WinBuffer>
bool update_texture_from_internal_image_object(Texture& texture, WinBuffer const& buffer)
{
    auto const image = buffer.internal.image;
    if (image.isNull()) {
        return false;
    }

    uint32_t format;

    // TODO(romangg): The Qt pixel formats depend on the endianness while DRM is always LE. So on BE
    //                machines QImage::Format_RGBA8888_Premultiplied would instead correspond to
    //                DRM_FORMAT_RGBX8888, see [1]. But at the same time Format_ARGB32_Premultiplied
    //                does not seem to be influenced. Need to test this on an actual BE machine to
    //                be sure.
    // [1] https://gitlab.freedesktop.org/wlroots/wlroots/-/merge_requests/3464#note_1277281
    switch (image.format()) {
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
        format = DRM_FORMAT_ARGB8888;
        break;
    case QImage::Format_RGB32:
        format = DRM_FORMAT_XBGR8888;
        break;
    default:
        return false;
    }

    QImage conv_image;
    if (Texture::s_supportsARGB32 && format == DRM_FORMAT_ARGB8888) {
        conv_image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } else {
        conv_image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    }

    // We know it's an internal image so access the damage without the virtual buffer damage call.
    // TODO(romangg): Wrap this into a helper function in render::wayland namespace.
    auto const& damage = std::visit(
        overload{[&](auto&& win) -> QRegion { return win->render_data.damage_region; }},
        *buffer.buffer.window->ref_win);

    return update_texture_from_data(texture,
                                    format,
                                    image.bytesPerLine(),
                                    image.size(),
                                    damage,
                                    image.devicePixelRatio(),
                                    conv_image.bits());
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
        qCDebug(KWIN_CORE) << "Failed to update texture via EGL/wl_drm";
        texture.q->discard();
        return false;
    }

    return true;
}

template<typename Texture, typename Dmabuf>
bool update_texture_from_dmabuf(Texture& texture, Dmabuf* dmabuf)
{
    assert(dmabuf);
    assert(texture.m_image == EGL_NO_IMAGE_KHR);

    if (texture.m_size != dmabuf->size) {
        // First time update or size has changed.
        // TODO(romangg): Should we also recreate the texture on other param changes?
        wlr_dmabuf_attributes dmabuf_attribs;
        auto const& planes = dmabuf->planes;
        dmabuf_attribs.width = dmabuf->size.width();
        dmabuf_attribs.height = dmabuf->size.height();
        dmabuf_attribs.format = dmabuf->format;
        dmabuf_attribs.modifier = dmabuf->modifier;
        dmabuf_attribs.n_planes = planes.size();

        auto planes_count = std::min(planes.size(), static_cast<size_t>(WLR_DMABUF_MAX_PLANES));
        for (size_t i = 0; i < planes_count; i++) {
            auto plane = planes.at(i);
            dmabuf_attribs.offset[i] = plane.offset;
            dmabuf_attribs.stride[i] = plane.stride;
            dmabuf_attribs.fd[i] = plane.fd;
        }

        wlr_texture_destroy(texture.native);
        texture.native
            = wlr_texture_from_dmabuf(texture.m_backend->platform.renderer, &dmabuf_attribs);
        if (!texture.native) {
            return false;
        }

        wlr_gles2_texture_attribs tex_attribs;
        wlr_gles2_texture_get_attribs(texture.native, &tex_attribs);

        texture.m_texture = tex_attribs.tex;
        texture.q->setWrapMode(GL_CLAMP_TO_EDGE);
        texture.q->setFilter(GL_NEAREST);
        texture.m_size = dmabuf->size;
        texture.updateMatrix();
    }

    assert(texture.native);

    // The origin in a dmabuf-buffer is at the upper-left corner, so the meaning
    // of Y-inverted is the inverse of OpenGL.
    if (dmabuf->flags & Wrapland::Server::linux_dmabuf_flag_v1::y_inverted) {
        texture.q->set_content_transform(effect::transform_type::normal);
    } else {
        texture.q->set_content_transform(effect::transform_type::flipped_180);
    }

    return true;
}

template<typename Texture>
bool update_texture_from_data(Texture& texture,
                              uint32_t format,
                              uint32_t stride,
                              QSize const& size,
                              QRegion const& damage,
                              int32_t scale,
                              void* data)
{
    if (size != texture.m_size) {
        // First time update or size has changed.
        wlr_texture_destroy(texture.native);
        texture.native = wlr_texture_from_pixels(texture.m_backend->platform.renderer,
                                                 format,
                                                 stride,
                                                 size.width(),
                                                 size.height(),
                                                 data);
        if (!texture.native) {
            return false;
        }

        wlr_gles2_texture_attribs tex_attribs;
        wlr_gles2_texture_get_attribs(texture.native, &tex_attribs);

        texture.m_texture = tex_attribs.tex;
        texture.q->unbind();
        texture.q->set_content_transform(effect::transform_type::flipped_180);
        texture.m_size = size;
        texture.updateMatrix();

        return true;
    }

    assert(size == texture.m_size);

    auto buffer
        = wlr_non_owning_data_buffer_create(size.width(), size.height(), format, stride, data);
    auto pixman_damage = create_scaled_pixman_region(damage, scale);

    wlr_texture_update_from_buffer(texture.native, &buffer->base, &pixman_damage);

    pixman_region32_fini(&pixman_damage);
    wlr_buffer_drop(&buffer->base);
    return true;
}

template<typename Texture, typename WinBuffer>
bool update_texture_from_shm(Texture& texture, WinBuffer const& buffer)
{
    auto const extbuf = buffer.external.get();
    assert(extbuf && extbuf->shmBuffer());

    auto image = extbuf->shmImage();
    auto surface = extbuf->surface();
    if (!image || !surface) {
        return false;
    }

    return update_texture_from_data(texture,
                                    image->format() == Wrapland::Server::ShmImage::Format::argb8888
                                        ? DRM_FORMAT_ARGB8888
                                        : DRM_FORMAT_XRGB8888,
                                    image->stride(),
                                    extbuf->size(),
                                    surface->trackedDamage(),
                                    surface->state().scale,
                                    image->data());
}

template<typename Texture, typename WinBuffer>
bool update_texture_from_external(Texture& texture, WinBuffer const& buffer)
{
    bool ret;
    auto const extbuf = buffer.external.get();
    assert(extbuf);

    if (auto dmabuf = extbuf->linuxDmabufBuffer()) {
        ret = update_texture_from_dmabuf(texture, dmabuf);
    } else if (auto shm = extbuf->shmBuffer()) {
        ret = update_texture_from_shm(texture, buffer);
    } else {
        ret = update_texture_from_egl(texture, extbuf);
    }

    if (auto surface = extbuf->surface()) {
        surface->resetTrackedDamage();
    }

    return ret;
}

template<typename Texture, typename WinBuffer>
bool update_texture_from_internal(Texture& texture, WinBuffer& buffer)
{
    assert(!buffer.external);

    return update_texture_from_fbo(texture, buffer.internal.fbo)
        || update_texture_from_internal_image_object(texture, buffer);
}

template<typename Texture, typename Buffer>
bool update_texture_from_buffer(Texture& texture, Buffer* buffer)
{
    auto& win_integrate
        = static_cast<render::wayland::buffer_win_integration<typename Buffer::abstract_type>&>(
            *buffer->win_integration);
    if (win_integrate.external) {
        return update_texture_from_external(texture, win_integrate);
    }
    return update_texture_from_internal(texture, win_integrate);
}

}
