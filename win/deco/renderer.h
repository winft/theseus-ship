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

#include "decorations_logging.h"

#include "kwin_export.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <QObject>
#include <QPainter>
#include <QRegion>
#include <memory>

namespace KWin::win::deco
{

class render_data
{
public:
    virtual ~render_data() = default;
};

class KWIN_EXPORT renderer_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void renderScheduled(QRegion const& geo);
};

template<typename Client>
class renderer
{
public:
    using qobject_t = renderer_qobject;

    virtual ~renderer() = default;

    void schedule(const QRegion& region)
    {
        m_scheduled = m_scheduled.united(region);
        Q_EMIT qobject->renderScheduled(region);
    }

    std::unique_ptr<win::deco::render_data> move_data()
    {
        render();
        m_client = nullptr;
        return std::move(data);
    }

    std::unique_ptr<renderer_qobject> qobject;
    std::unique_ptr<render_data> data;

protected:
    explicit renderer(Client* client)
        : qobject{std::make_unique<renderer_qobject>()}
        , m_client(client)
        , m_imageSizesDirty(true)
    {
        auto markImageSizesDirty = [this] { m_imageSizesDirty = true; };
        QObject::connect(client->decoration(),
                         &KDecoration2::Decoration::damaged,
                         qobject.get(),
                         [this](auto const& rect) { schedule(rect); });
        QObject::connect(client->client()->qobject.get(),
                         &decltype(client->client()->qobject)::element_type::central_output_changed,
                         qobject.get(),
                         [markImageSizesDirty](auto old_out, auto new_out) {
                             if (!new_out) {
                                 return;
                             }
                             if (old_out && old_out->scale() == new_out->scale()) {
                                 return;
                             }
                             markImageSizesDirty();
                         });
        QObject::connect(client->decoration(),
                         &KDecoration2::Decoration::bordersChanged,
                         qobject.get(),
                         markImageSizesDirty);
        QObject::connect(client->decoratedClient(),
                         &KDecoration2::DecoratedClient::widthChanged,
                         qobject.get(),
                         markImageSizesDirty);
        QObject::connect(client->decoratedClient(),
                         &KDecoration2::DecoratedClient::heightChanged,
                         qobject.get(),
                         markImageSizesDirty);
    }

    /**
     * @returns the scheduled paint region and resets
     */
    QRegion getScheduled()
    {
        QRegion region = m_scheduled;
        m_scheduled = QRegion();
        return region;
    }

    virtual void render() = 0;

    Client* client()
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
    QImage renderToImage(const QRect& geo)
    {
        Q_ASSERT(m_client);
        auto window = m_client->client();
        auto dpr = window->topo.central_output ? window->topo.central_output->scale() : 1.;

        // Guess the pixel format of the X pixmap into which the QImage will be copied.
        QImage::Format format;
        const int depth = window->render_data.bit_depth;
        switch (depth) {
        case 30:
            format = QImage::Format_A2RGB30_Premultiplied;
            break;
        case 24:
        case 32:
            format = QImage::Format_ARGB32_Premultiplied;
            break;
        default:
            qCCritical(KWIN_DECORATIONS) << "Unsupported client depth" << depth;
            format = QImage::Format_ARGB32_Premultiplied;
            break;
        };

        QImage image(geo.width() * dpr, geo.height() * dpr, format);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setWindow(QRect(geo.topLeft(), geo.size() * dpr));
        p.setClipRect(geo);
        renderToPainter(&p, geo);
        return image;
    }

    void renderToPainter(QPainter* painter, const QRect& rect)
    {
        m_client->decoration()->paint(painter, rect);
    }

private:
    Client* m_client;
    QRegion m_scheduled;
    bool m_imageSizesDirty;
};

}
