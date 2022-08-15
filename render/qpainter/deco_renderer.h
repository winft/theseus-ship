/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/deco/renderer.h"

#include <QImage>

namespace KWin::render::qpainter
{

enum class DecorationPart : int {
    Left,
    Top,
    Right,
    Bottom,
    Count,
};

template<typename Client>
class deco_renderer : public win::deco::renderer<Client>
{
public:
    explicit deco_renderer(Client* client)
        : win::deco::renderer<Client>(client)
    {
        QObject::connect(this->qobject.get(),
                         &win::deco::renderer_qobject::renderScheduled,
                         client->client()->qobject.get(),
                         [win = client->client()](auto const& region) { win->addRepaint(region); });
    }

    void render() override
    {
        auto const scheduled = this->getScheduled();
        if (scheduled.isEmpty()) {
            return;
        }
        if (this->areImageSizesDirty()) {
            resizeImages();
            this->resetImageSizesDirty();
        }

        auto imageSize = [this](DecorationPart part) {
            return m_images[int(part)].size() / m_images[int(part)].devicePixelRatio();
        };

        const QRect top(QPoint(0, 0), imageSize(DecorationPart::Top));
        const QRect left(QPoint(0, top.height()), imageSize(DecorationPart::Left));
        const QRect right(
            QPoint(top.width() - imageSize(DecorationPart::Right).width(), top.height()),
            imageSize(DecorationPart::Right));
        const QRect bottom(QPoint(0, left.y() + left.height()), imageSize(DecorationPart::Bottom));

        const QRect geometry = scheduled.boundingRect();
        auto renderPart = [this](const QRect& rect, const QRect& partRect, int index) {
            if (rect.isEmpty()) {
                return;
            }
            QPainter painter(&m_images[index]);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setWindow(
                QRect(partRect.topLeft(), partRect.size() * m_images[index].devicePixelRatio()));
            painter.setClipRect(rect);
            painter.save();
            // clear existing part
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(rect, Qt::transparent);
            painter.restore();
            this->client()->decoration()->paint(&painter, rect);
        };

        renderPart(left.intersected(geometry), left, int(DecorationPart::Left));
        renderPart(top.intersected(geometry), top, int(DecorationPart::Top));
        renderPart(right.intersected(geometry), right, int(DecorationPart::Right));
        renderPart(bottom.intersected(geometry), bottom, int(DecorationPart::Bottom));
    }

    void reparent() override
    {
        render();
        win::deco::renderer<Client>::reparent();
    }

    QImage image(DecorationPart part) const
    {
        Q_ASSERT(part != DecorationPart::Count);
        return m_images[int(part)];
    }

private:
    void resizeImages()
    {
        QRect left, top, right, bottom;
        auto window = this->client()->client();
        window->layoutDecorationRects(left, top, right, bottom);

        auto checkAndCreate = [this, window](int index, const QSize& size) {
            auto dpr = window->central_output ? window->central_output->scale() : 1.;
            if (m_images[index].size() != size * dpr || m_images[index].devicePixelRatio() != dpr) {
                m_images[index] = QImage(size * dpr, QImage::Format_ARGB32_Premultiplied);
                m_images[index].setDevicePixelRatio(dpr);
                m_images[index].fill(Qt::transparent);
            }
        };
        checkAndCreate(int(DecorationPart::Left), left.size());
        checkAndCreate(int(DecorationPart::Right), right.size());
        checkAndCreate(int(DecorationPart::Top), top.size());
        checkAndCreate(int(DecorationPart::Bottom), bottom.size());
    }

    QImage m_images[int(DecorationPart::Count)];
};

}
