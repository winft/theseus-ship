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
    : Wrapland::Server::LinuxDmabufBufferV1(format, size)
    , m_planes(planes)
    , m_format(format)
    , m_size(size)
    , m_flags(flags)
{
    waylandServer()->dmabuf_buffers << this;
}

dmabuf_buffer::~dmabuf_buffer()
{
    // Close all open file descriptors
    for (int i = 0; i < m_planes.count(); i++) {
        if (m_planes[i].fd != -1)
            ::close(m_planes[i].fd);
        m_planes[i].fd = -1;
    }
    if (auto server = waylandServer()) {
        server->dmabuf_buffers.remove(this);
    }
}

linux_dmabuf::linux_dmabuf()
    : Wrapland::Server::LinuxDmabufV1::Impl()
{
    Q_ASSERT(waylandServer());
    waylandServer()->linux_dmabuf()->setImpl(this);
}

linux_dmabuf::~linux_dmabuf()
{
    waylandServer()->linux_dmabuf()->setImpl(nullptr);
}

using Plane = Wrapland::Server::LinuxDmabufV1::Plane;
using Flags = Wrapland::Server::LinuxDmabufV1::Flags;

Wrapland::Server::LinuxDmabufBufferV1* linux_dmabuf::importBuffer(const QVector<Plane>& planes,
                                                                  uint32_t format,
                                                                  const QSize& size,
                                                                  Flags flags)
{
    Q_UNUSED(planes)
    Q_UNUSED(format)
    Q_UNUSED(size)
    Q_UNUSED(flags)

    return nullptr;
}

void linux_dmabuf::setSupportedFormatsAndModifiers(QHash<uint32_t, QSet<uint64_t>>& set)
{
    waylandServer()->linux_dmabuf()->setSupportedFormatsWithModifiers(set);
}

}
