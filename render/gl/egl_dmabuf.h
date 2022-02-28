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
    egl_dmabuf_buffer(std::vector<Wrapland::Server::linux_dmabuf_plane_v1> planes,
                      uint32_t format,
                      uint64_t modifier,
                      const QSize& size,
                      Wrapland::Server::linux_dmabuf_flags_v1 flags);
};

class KWIN_EXPORT egl_dmabuf
{
public:
    using Plane = Wrapland::Server::linux_dmabuf_plane_v1;
    using Flags = Wrapland::Server::linux_dmabuf_flags_v1;

    egl_dmabuf();

    std::unique_ptr<Wrapland::Server::linux_dmabuf_buffer_v1>
    import_buffer(std::vector<Plane> const& planes,
                  uint32_t format,
                  uint64_t modifier,
                  const QSize& size,
                  Flags flags);
};

template<typename Backend>
static egl_dmabuf* egl_dmabuf_factory(Backend& backend)
{
    assert(backend.data.base.display != EGL_NO_DISPLAY);

    if (!backend.hasExtension(QByteArrayLiteral("EGL_EXT_image_dma_buf_import"))) {
        return nullptr;
    }

    return new egl_dmabuf;
}

}
