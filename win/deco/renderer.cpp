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
#include "renderer.h"

#include "client_impl.h"
#include "decorations_logging.h"

#include "toplevel.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>

#include <QDebug>
#include <QPainter>

namespace KWin::win::deco
{

renderer::renderer(client_impl* client)
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
                     &win::window_qobject::central_output_changed,
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

renderer::~renderer() = default;

void renderer::schedule(const QRegion& rect)
{
    m_scheduled = m_scheduled.united(rect);
    Q_EMIT qobject->renderScheduled(rect);
}

QRegion renderer::getScheduled()
{
    QRegion region = m_scheduled;
    m_scheduled = QRegion();
    return region;
}

QImage renderer::renderToImage(const QRect& geo)
{
    Q_ASSERT(m_client);
    auto window = client()->client();
    auto dpr = window->central_output ? window->central_output->scale() : 1.;

    // Guess the pixel format of the X pixmap into which the QImage will be copied.
    QImage::Format format;
    const int depth = window->bit_depth;
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

void renderer::renderToPainter(QPainter* painter, const QRect& rect)
{
    client()->decoration()->paint(painter, rect);
}

void renderer::reparent()
{
    m_client = nullptr;
}

}
