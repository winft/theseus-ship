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

egl_dmabuf::egl_dmabuf()
{
    // TODO(romangg): Could we just reset it? I.e. recreate the global.
    auto& dmabuf = waylandServer()->globals->linux_dmabuf_v1;
    assert(!dmabuf);
    dmabuf = std::make_unique<Wrapland::Server::linux_dmabuf_v1>(
        waylandServer()->display.get(),
        [this](auto const& planes, auto format, auto modifier, auto const& size, auto flags) {
            return import_buffer(planes, format, modifier, size, flags);
        });
}

}
