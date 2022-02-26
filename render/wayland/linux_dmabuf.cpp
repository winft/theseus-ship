/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright © 2019 Roman Gilg <subdiff@gmail.com>

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
#include "linux_dmabuf.h"

#include "base/wayland/server.h"
#include "main.h"

#include <unistd.h>

namespace KWin::render::wayland
{

dmabuf_buffer::dmabuf_buffer(const QVector<Plane>& planes,
                             uint32_t format,
                             const QSize& size,
                             Flags flags)
    : Wrapland::Server::linux_dmabuf_buffer_v1(format, size)
    , m_planes(planes)
    , m_flags(flags)
{
}

dmabuf_buffer::~dmabuf_buffer()
{
    // Close all open file descriptors
    for (int i = 0; i < m_planes.count(); i++) {
        if (m_planes[i].fd != -1)
            ::close(m_planes[i].fd);
        m_planes[i].fd = -1;
    }
}

}
