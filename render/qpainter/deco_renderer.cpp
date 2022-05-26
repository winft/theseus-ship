/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "deco_renderer.h"

#include "effect_frame.h"
#include "shadow.h"
#include "window.h"

#include "base/output.h"
#include "input/cursor.h"
#include "main.h"
#include "render/compositor.h"
#include "render/cursor.h"
#include "toplevel.h"
#include "win/deco/client_impl.h"

#include <KDecoration2/Decoration>
#include <QPainter>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>

#include <cmath>

namespace KWin::render::qpainter
{

deco_renderer::deco_renderer(win::deco::client_impl* client)
    : renderer(client)
{
    connect(this,
            &renderer::renderScheduled,
            client->client(),
            static_cast<void (Toplevel::*)(QRegion const&)>(&Toplevel::addRepaint));
}

deco_renderer::~deco_renderer() = default;

QImage deco_renderer::image(deco_renderer::DecorationPart part) const
{
    Q_ASSERT(part != DecorationPart::Count);
    return m_images[int(part)];
}

void deco_renderer::render()
{
    const QRegion scheduled = getScheduled();
    if (scheduled.isEmpty()) {
        return;
    }
    if (areImageSizesDirty()) {
        resizeImages();
        resetImageSizesDirty();
    }

    auto imageSize = [this](DecorationPart part) {
        return m_images[int(part)].size() / m_images[int(part)].devicePixelRatio();
    };

    const QRect top(QPoint(0, 0), imageSize(DecorationPart::Top));
    const QRect left(QPoint(0, top.height()), imageSize(DecorationPart::Left));
    const QRect right(QPoint(top.width() - imageSize(DecorationPart::Right).width(), top.height()),
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
        client()->decoration()->paint(&painter, rect);
    };

    renderPart(left.intersected(geometry), left, int(DecorationPart::Left));
    renderPart(top.intersected(geometry), top, int(DecorationPart::Top));
    renderPart(right.intersected(geometry), right, int(DecorationPart::Right));
    renderPart(bottom.intersected(geometry), bottom, int(DecorationPart::Bottom));
}

void deco_renderer::resizeImages()
{
    QRect left, top, right, bottom;
    auto window = client()->client();
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

void deco_renderer::reparent()
{
    render();
    renderer::reparent();
}

}
