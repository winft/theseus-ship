/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

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

#include <QObject>
#include <QRegion>
#include <memory>

namespace KWin::win::deco
{

class client_impl;

class KWIN_EXPORT renderer_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void renderScheduled(QRegion const& geo);
};

class KWIN_EXPORT renderer
{
public:
    using qobject_t = renderer_qobject;

    virtual ~renderer();

    void schedule(const QRegion& region);

    /// After this call the renderer is no longer able to render anything, client() returns null.
    virtual void reparent();

    std::unique_ptr<renderer_qobject> qobject;

protected:
    explicit renderer(client_impl* client);
    /**
     * @returns the scheduled paint region and resets
     */
    QRegion getScheduled();

    virtual void render() = 0;

    client_impl* client()
    {
        return m_client;
    }

    bool areImageSizesDirty() const
    {
        return m_imageSizesDirty;
    }
    void resetImageSizesDirty()
    {
        m_imageSizesDirty = false;
    }
    QImage renderToImage(const QRect& geo);
    void renderToPainter(QPainter* painter, const QRect& rect);

private:
    client_impl* m_client;
    QRegion m_scheduled;
    bool m_imageSizesDirty;
};

}
