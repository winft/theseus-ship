/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright © 2019 Roman Gilg <subdiff@gmail.com>
Copyright © 2018 Fredrik Höglund <fredrik@kde.org>

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
#include "egl_dmabuf.h"

#include "kwin_eglext.h"

#include "base/wayland/server.h"
#include "main.h"
#include "render/extern/drm_fourcc.h"

#include <Wrapland/Server/globals.h>
#include <unistd.h>

namespace KWin::render::gl
{

struct YuvPlane {
    int widthDivisor = 0;
    int heightDivisor = 0;
    uint32_t format = 0;
    int planeIndex = 0;
};

struct YuvFormat {
    uint32_t format = 0;
    int inputPlanes = 0;
    int outputPlanes = 0;
    int textureType = 0;
    struct YuvPlane planes[3];
};

YuvFormat yuvFormats[]
    = {{DRM_FORMAT_YUYV,
        1,
        2,
        EGL_TEXTURE_Y_XUXV_WL,
        {{1, 1, DRM_FORMAT_GR88, 0}, {2, 1, DRM_FORMAT_ARGB8888, 0}}},
       {DRM_FORMAT_NV12,
        2,
        2,
        EGL_TEXTURE_Y_UV_WL,
        {{1, 1, DRM_FORMAT_R8, 0}, {2, 2, DRM_FORMAT_GR88, 1}}},
       {DRM_FORMAT_YUV420,
        3,
        3,
        EGL_TEXTURE_Y_U_V_WL,
        {{1, 1, DRM_FORMAT_R8, 0}, {2, 2, DRM_FORMAT_R8, 1}, {2, 2, DRM_FORMAT_R8, 2}}},
       {DRM_FORMAT_YUV444,
        3,
        3,
        EGL_TEXTURE_Y_U_V_WL,
        {{1, 1, DRM_FORMAT_R8, 0}, {1, 1, DRM_FORMAT_R8, 1}, {1, 1, DRM_FORMAT_R8, 2}}}};

egl_dmabuf_buffer::egl_dmabuf_buffer(EGLImage image,
                                     const QVector<Plane>& planes,
                                     uint32_t format,
                                     const QSize& size,
                                     Flags flags,
                                     egl_dmabuf* interfaceImpl)
    : egl_dmabuf_buffer(planes, format, size, flags, interfaceImpl)
{
    m_importType = ImportType::Direct;
    addImage(image);
}

egl_dmabuf_buffer::egl_dmabuf_buffer(const QVector<Plane>& planes,
                                     uint32_t format,
                                     const QSize& size,
                                     Flags flags,
                                     egl_dmabuf* interfaceImpl)
    : wayland::dmabuf_buffer(planes, format, size, flags)
    , m_interfaceImpl(interfaceImpl)
{
    m_importType = ImportType::Conversion;
}

egl_dmabuf_buffer::~egl_dmabuf_buffer()
{
    removeImages();
}

std::vector<EGLImage> const& egl_dmabuf_buffer::images() const
{
    return m_images;
}

void egl_dmabuf_buffer::addImage(EGLImage image)
{
    m_images.push_back(image);
}

void egl_dmabuf_buffer::removeImages()
{
    if (m_interfaceImpl) {
        for (auto image : m_images) {
            m_interfaceImpl->data.base.destroy_image_khr(m_interfaceImpl->data.base.display, image);
        }
    }
    m_images.clear();
}

using Plane = Wrapland::Server::linux_dmabuf_plane_v1;
using Flags = Wrapland::Server::linux_dmabuf_flags_v1;

EGLImage egl_dmabuf::createImage(const QVector<Plane>& planes, uint32_t format, const QSize& size)
{
    const bool hasModifiers
        = eglQueryDmaBufModifiersEXT != nullptr && planes[0].modifier != DRM_FORMAT_MOD_INVALID;

    QVector<EGLint> attribs;
    attribs << EGL_WIDTH << size.width() << EGL_HEIGHT << size.height() << EGL_LINUX_DRM_FOURCC_EXT
            << EGLint(format)

            << EGL_DMA_BUF_PLANE0_FD_EXT << planes[0].fd << EGL_DMA_BUF_PLANE0_OFFSET_EXT
            << EGLint(planes[0].offset) << EGL_DMA_BUF_PLANE0_PITCH_EXT << EGLint(planes[0].stride);

    if (hasModifiers) {
        attribs << EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT << EGLint(planes[0].modifier & 0xffffffff)
                << EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT << EGLint(planes[0].modifier >> 32);
    }

    if (planes.count() > 1) {
        attribs << EGL_DMA_BUF_PLANE1_FD_EXT << planes[1].fd << EGL_DMA_BUF_PLANE1_OFFSET_EXT
                << EGLint(planes[1].offset) << EGL_DMA_BUF_PLANE1_PITCH_EXT
                << EGLint(planes[1].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT << EGLint(planes[1].modifier & 0xffffffff)
                    << EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT << EGLint(planes[1].modifier >> 32);
        }
    }

    if (planes.count() > 2) {
        attribs << EGL_DMA_BUF_PLANE2_FD_EXT << planes[2].fd << EGL_DMA_BUF_PLANE2_OFFSET_EXT
                << EGLint(planes[2].offset) << EGL_DMA_BUF_PLANE2_PITCH_EXT
                << EGLint(planes[2].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT << EGLint(planes[2].modifier & 0xffffffff)
                    << EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT << EGLint(planes[2].modifier >> 32);
        }
    }

    if (eglQueryDmaBufModifiersEXT != nullptr && planes.count() > 3) {
        attribs << EGL_DMA_BUF_PLANE3_FD_EXT << planes[3].fd << EGL_DMA_BUF_PLANE3_OFFSET_EXT
                << EGLint(planes[3].offset) << EGL_DMA_BUF_PLANE3_PITCH_EXT
                << EGLint(planes[3].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT << EGLint(planes[3].modifier & 0xffffffff)
                    << EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT << EGLint(planes[3].modifier >> 32);
        }
    }

    attribs << EGL_NONE;

    auto image = data.base.create_image_khr(data.base.display,
                                            EGL_NO_CONTEXT,
                                            EGL_LINUX_DMA_BUF_EXT,
                                            (EGLClientBuffer) nullptr,
                                            attribs.data());
    if (image == EGL_NO_IMAGE_KHR) {
        return nullptr;
    }

    return image;
}

std::unique_ptr<Wrapland::Server::linux_dmabuf_buffer_v1>
egl_dmabuf::import_buffer(QVector<Plane> const& planes,
                          uint32_t format,
                          const QSize& size,
                          Flags flags)
{
    Q_ASSERT(planes.count() > 0);

    // Try first to import as a single image
    if (auto* img = createImage(planes, format, size)) {
        return std::make_unique<egl_dmabuf_buffer>(img, planes, format, size, flags, this);
    }

    // TODO: to enable this we must be able to store multiple textures per window pixmap
    //       and when on window draw do yuv to rgb transformation per shader (see Weston)
    //    // not a single image, try yuv import
    //    return yuvImport(planes, format, size, flags);

    return nullptr;
}

Wrapland::Server::linux_dmabuf_buffer_v1*
egl_dmabuf::yuvImport(const QVector<Plane>& planes, uint32_t format, const QSize& size, Flags flags)
{
    YuvFormat yuvFormat;
    for (YuvFormat f : yuvFormats) {
        if (f.format == format) {
            yuvFormat = f;
            break;
        }
    }
    if (yuvFormat.format == 0) {
        return nullptr;
    }
    if (planes.count() != yuvFormat.inputPlanes) {
        return nullptr;
    }

    auto* buf = new egl_dmabuf_buffer(planes, format, size, flags, this);

    for (int i = 0; i < yuvFormat.outputPlanes; i++) {
        int planeIndex = yuvFormat.planes[i].planeIndex;
        Plane plane = {planes[planeIndex].fd,
                       planes[planeIndex].offset,
                       planes[planeIndex].stride,
                       planes[planeIndex].modifier};
        const auto planeFormat = yuvFormat.planes[i].format;
        const auto planeSize = QSize(size.width() / yuvFormat.planes[i].widthDivisor,
                                     size.height() / yuvFormat.planes[i].heightDivisor);
        auto* image = createImage(QVector<Plane>(1, plane), planeFormat, planeSize);
        if (!image) {
            delete buf;
            return nullptr;
        }
        buf->addImage(image);
    }
    // TODO: add buf import properties
    return buf;
}

egl_dmabuf::egl_dmabuf(egl_dmabuf_data const& data)
    : data{data}
{
    // TODO(romangg): Could we just reset it? I.e. recreate the global.
    auto& dmabuf = waylandServer()->globals->linux_dmabuf_v1;
    assert(!dmabuf);
    dmabuf = std::make_unique<Wrapland::Server::linux_dmabuf_v1>(
        waylandServer()->display.get(),
        [this](auto const& planes, auto format, auto const& size, auto flags) {
            return import_buffer(planes, format, size, flags);
        });

    setSupportedFormatsAndModifiers();
}

const uint32_t s_multiPlaneFormats[] = {
    DRM_FORMAT_XRGB8888_A8, DRM_FORMAT_XBGR8888_A8, DRM_FORMAT_RGBX8888_A8, DRM_FORMAT_BGRX8888_A8,
    DRM_FORMAT_RGB888_A8,   DRM_FORMAT_BGR888_A8,   DRM_FORMAT_RGB565_A8,   DRM_FORMAT_BGR565_A8,

    DRM_FORMAT_NV12,        DRM_FORMAT_NV21,        DRM_FORMAT_NV16,        DRM_FORMAT_NV61,
    DRM_FORMAT_NV24,        DRM_FORMAT_NV42,

    DRM_FORMAT_YUV410,      DRM_FORMAT_YVU410,      DRM_FORMAT_YUV411,      DRM_FORMAT_YVU411,
    DRM_FORMAT_YUV420,      DRM_FORMAT_YVU420,      DRM_FORMAT_YUV422,      DRM_FORMAT_YVU422,
    DRM_FORMAT_YUV444,      DRM_FORMAT_YVU444};

// Following formats are in Weston as a fallback. XYUV8888 is the only one not in our drm_fourcc.h
// Weston does define it itself for older kernels. But for now just use the other ones.
uint32_t const s_fallbackFormats[] = {
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_YUYV,
    DRM_FORMAT_NV12,
    DRM_FORMAT_YUV420,
    DRM_FORMAT_YUV444,
    //    DRM_FORMAT_XYUV8888,
};

void filterFormatsWithMultiplePlanes(QVector<uint32_t>& formats)
{
    QVector<uint32_t>::iterator it = formats.begin();
    while (it != formats.end()) {
        for (auto linuxFormat : s_multiPlaneFormats) {
            if (*it == linuxFormat) {
                qDebug() << "Filter multi-plane format" << *it;
                it = formats.erase(it);
                it--;
                break;
            }
        }
        it++;
    }
}

QVector<uint32_t> egl_dmabuf::queryFormats()
{
    if (!eglQueryDmaBufFormatsEXT) {
        return QVector<uint32_t>();
    }

    EGLint count = 0;
    EGLBoolean success = eglQueryDmaBufFormatsEXT(data.base.display, 0, nullptr, &count);
    if (!success || count == 0) {
        return QVector<uint32_t>();
    }

    QVector<uint32_t> formats(count);
    if (!eglQueryDmaBufFormatsEXT(data.base.display, count, (EGLint*)formats.data(), &count)) {
        return QVector<uint32_t>();
    }
    return formats;
}

void egl_dmabuf::setSupportedFormatsAndModifiers()
{
    auto formats = queryFormats();
    if (formats.count() == 0) {
        for (auto format : s_fallbackFormats) {
            formats << format;
        }
    }
    filterFormatsWithMultiplePlanes(formats);

    std::vector<Wrapland::Server::drm_format> drm_formats;

    for (auto format : qAsConst(formats)) {
        Wrapland::Server::drm_format drm_format;
        drm_format.format = format;
        if (eglQueryDmaBufModifiersEXT != nullptr) {
            EGLint count = 0;
            EGLBoolean success = eglQueryDmaBufModifiersEXT(
                data.base.display, format, 0, nullptr, nullptr, &count);

            if (success && count > 0) {
                QVector<uint64_t> modifiers(count);
                if (eglQueryDmaBufModifiersEXT(
                        data.base.display, format, count, modifiers.data(), nullptr, &count)) {
                    for (auto mod : qAsConst(modifiers)) {
                        drm_format.modifiers.insert(mod);
                    }
                }
            }
        }
        drm_formats.push_back(drm_format);
    }

    waylandServer()->linux_dmabuf()->set_formats(drm_formats);
}

}
