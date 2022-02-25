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
#pragma once

#include "kwin_export.h"

#include <QVector>
#include <Wrapland/Server/linux_dmabuf_v1.h>

namespace KWin::render::wayland
{

class KWIN_EXPORT dmabuf_buffer : public Wrapland::Server::linux_dmabuf_buffer_v1
{
public:
    using Plane = Wrapland::Server::linux_dmabuf_plane_v1;
    using Flags = Wrapland::Server::linux_dmabuf_flags_v1;

    dmabuf_buffer(const QVector<Plane>& planes, uint32_t format, const QSize& size, Flags flags);
    ~dmabuf_buffer() override;

    const QVector<Plane>& planes() const
    {
        return m_planes;
    }
    uint32_t format() const
    {
        return m_format;
    }
    QSize size() const
    {
        return m_size;
    }
    Flags flags() const
    {
        return m_flags;
    }

private:
    QVector<Plane> m_planes;
    uint32_t m_format;
    QSize m_size;
    Flags m_flags;
};

class KWIN_EXPORT linux_dmabuf : public Wrapland::Server::linux_dmabuf_v1::Impl
{
public:
    using Plane = Wrapland::Server::linux_dmabuf_plane_v1;
    using Flags = Wrapland::Server::linux_dmabuf_flags_v1;

    explicit linux_dmabuf();
    ~linux_dmabuf() override;

    Wrapland::Server::linux_dmabuf_buffer_v1* importBuffer(const QVector<Plane>& planes,
                                                           uint32_t format,
                                                           const QSize& size,
                                                           Flags flags) override;

protected:
    void setSupportedFormatsAndModifiers(QHash<uint32_t, QSet<uint64_t>>& set);
};

}
