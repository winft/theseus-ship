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
#pragma once

#include "egl_data.h"

#include "kwin_export.h"

#include <QVector>
#include <Wrapland/Server/linux_dmabuf_v1.h>
#include <cassert>
#include <epoxy/egl.h>

namespace KWin::render::gl
{
class egl_dmabuf;

class KWIN_EXPORT egl_dmabuf_buffer : public Wrapland::Server::linux_dmabuf_buffer_v1
{
public:
    using Plane = Wrapland::Server::linux_dmabuf_plane_v1;
    using Flags = Wrapland::Server::linux_dmabuf_flags_v1;

    enum class ImportType { Direct, Conversion };

    egl_dmabuf_buffer(EGLImage image,
                      std::vector<Plane> planes,
                      uint32_t format,
                      uint64_t modifier,
                      const QSize& size,
                      Flags flags,
                      egl_dmabuf* interfaceImpl);

    egl_dmabuf_buffer(std::vector<Plane> planes,
                      uint32_t format,
                      uint64_t modifier,
                      const QSize& size,
                      Flags flags,
                      egl_dmabuf* interfaceImpl);

    ~egl_dmabuf_buffer() override;

    void addImage(EGLImage image);
    void removeImages();

    std::vector<EGLImage> const& images() const;

private:
    std::vector<EGLImage> m_images;
    egl_dmabuf* m_interfaceImpl;
    ImportType m_importType;
};

struct egl_dmabuf_data {
    egl_data base;

    using query_formats_ext_func
        = EGLBoolean (*)(EGLDisplay dpy, EGLint max_formats, EGLint* formats, EGLint* num_formats);
    using query_modifiers_ext_func = EGLBoolean (*)(EGLDisplay dpy,
                                                    EGLint format,
                                                    EGLint max_modifiers,
                                                    EGLuint64KHR* modifiers,
                                                    EGLBoolean* external_only,
                                                    EGLint* num_modifiers);

    query_formats_ext_func query_formats_ext{nullptr};
    query_modifiers_ext_func query_modifiers_ext{nullptr};
};

class KWIN_EXPORT egl_dmabuf
{
public:
    using Plane = Wrapland::Server::linux_dmabuf_plane_v1;
    using Flags = Wrapland::Server::linux_dmabuf_flags_v1;

    explicit egl_dmabuf(egl_dmabuf_data const& data);

    std::unique_ptr<Wrapland::Server::linux_dmabuf_buffer_v1>
    import_buffer(std::vector<Plane> const& planes,
                  uint32_t format,
                  uint64_t modifier,
                  const QSize& size,
                  Flags flags);

    egl_dmabuf_data data;

private:
    EGLImage createImage(std::vector<Plane> const& planes,
                         uint32_t format,
                         uint64_t modifier,
                         const QSize& size);

    Wrapland::Server::linux_dmabuf_buffer_v1* yuvImport(std::vector<Plane> const& planes,
                                                        uint32_t format,
                                                        uint64_t modifier,
                                                        const QSize& size,
                                                        Flags flags);
    QVector<uint32_t> queryFormats();
    void setSupportedFormatsAndModifiers();

    friend class egl_dmabuf_buffer;
};

template<typename Backend>
static egl_dmabuf* egl_dmabuf_factory(Backend& backend)
{
    assert(backend.data.base.display != EGL_NO_DISPLAY);

    if (!backend.hasExtension(QByteArrayLiteral("EGL_EXT_image_dma_buf_import"))) {
        return nullptr;
    }

    egl_dmabuf_data data;
    data.base = backend.data.base;

    if (backend.hasExtension(QByteArrayLiteral("EGL_EXT_image_dma_buf_import_modifiers"))) {
        data.query_formats_ext = reinterpret_cast<egl_dmabuf_data::query_formats_ext_func>(
            eglGetProcAddress("eglQueryDmaBufFormatsEXT"));
        data.query_modifiers_ext = reinterpret_cast<egl_dmabuf_data::query_modifiers_ext_func>(
            eglGetProcAddress("eglQueryDmaBufModifiersEXT"));
    }

    return new egl_dmabuf(data);
}

}
