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

#include "egl_backend.h"

#include "render/wayland/linux_dmabuf.h"

#include <QVector>

namespace KWin::render::gl
{
class egl_dmabuf;

class egl_dmabuf_buffer : public wayland::dmabuf_buffer
{
public:
    using Plane = Wrapland::Server::LinuxDmabufV1::Plane;
    using Flags = Wrapland::Server::LinuxDmabufV1::Flags;

    enum class ImportType { Direct, Conversion };

    egl_dmabuf_buffer(EGLImage image,
                      const QVector<Plane>& planes,
                      uint32_t format,
                      const QSize& size,
                      Flags flags,
                      egl_dmabuf* interfaceImpl);

    egl_dmabuf_buffer(const QVector<Plane>& planes,
                      uint32_t format,
                      const QSize& size,
                      Flags flags,
                      egl_dmabuf* interfaceImpl);

    ~egl_dmabuf_buffer() override;

    void setInterfaceImplementation(egl_dmabuf* interfaceImpl);
    void addImage(EGLImage image);
    void removeImages();

    std::vector<EGLImage> const& images() const;

private:
    std::vector<EGLImage> m_images;
    egl_dmabuf* m_interfaceImpl;
    ImportType m_importType;
};

class egl_dmabuf : public wayland::linux_dmabuf
{
public:
    using Plane = Wrapland::Server::LinuxDmabufV1::Plane;
    using Flags = Wrapland::Server::LinuxDmabufV1::Flags;

    static egl_dmabuf* factory(egl_backend* backend);

    explicit egl_dmabuf(egl_backend* backend);
    ~egl_dmabuf() override;

    Wrapland::Server::LinuxDmabufBufferV1* importBuffer(const QVector<Plane>& planes,
                                                        uint32_t format,
                                                        const QSize& size,
                                                        Flags flags) override;

private:
    EGLImage createImage(const QVector<Plane>& planes, uint32_t format, const QSize& size);

    Wrapland::Server::LinuxDmabufBufferV1*
    yuvImport(const QVector<Plane>& planes, uint32_t format, const QSize& size, Flags flags);
    QVector<uint32_t> queryFormats();
    void setSupportedFormatsAndModifiers();

    egl_backend* m_backend;

    friend class egl_dmabuf_buffer;
};

}
