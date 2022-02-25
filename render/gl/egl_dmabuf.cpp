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

#include <Wrapland/Server/globals.h>
#include <drm_fourcc.h>
#include <unistd.h>

namespace KWin::render::gl
{

using Plane = Wrapland::Server::linux_dmabuf_plane_v1;
using Flags = Wrapland::Server::linux_dmabuf_flags_v1;

egl_dmabuf_buffer::egl_dmabuf_buffer(std::vector<Plane> planes,
                                     uint32_t format,
                                     uint64_t modifier,
                                     const QSize& size,
                                     Flags flags)
    : Wrapland::Server::linux_dmabuf_buffer_v1(std::move(planes), format, modifier, size, flags)
{
}

std::unique_ptr<Wrapland::Server::linux_dmabuf_buffer_v1>
egl_dmabuf::import_buffer(std::vector<Plane> const& planes,
                          uint32_t format,
                          uint64_t modifier,
                          const QSize& size,
                          Flags flags)
{
    Q_ASSERT(planes.size() > 0);
    return std::make_unique<egl_dmabuf_buffer>(planes, format, modifier, size, flags);
}

egl_dmabuf::egl_dmabuf(egl_dmabuf_data const& data)
    : data{data}
{
    // TODO(romangg): Could we just reset it? I.e. recreate the global.
    auto& dmabuf = waylandServer()->globals->linux_dmabuf_v1;
    assert(!dmabuf);
    dmabuf = std::make_unique<Wrapland::Server::linux_dmabuf_v1>(
        waylandServer()->display.get(),
        [this](auto const& planes, auto format, auto modifier, auto const& size, auto flags) {
            return import_buffer(planes, format, modifier, size, flags);
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
    if (!eglQueryDmaBufFormatsEXT(
            data.base.display, count, reinterpret_cast<EGLint*>(formats.data()), &count)) {
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
