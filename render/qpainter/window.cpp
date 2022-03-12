/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "buffer.h"
#include "deco_renderer.h"
#include "scene.h"
#include "shadow.h"

#include "win/geo.h"
#include "win/x11/window.h"

#include <QPainter>
#include <Wrapland/Server/surface.h>

namespace KWin::render::qpainter
{

window::window(qpainter::scene* scene, Toplevel* c)
    : render::window(c)
    , m_scene(scene)
{
}

window::~window()
{
}

static bool isXwaylandClient(Toplevel* toplevel)
{
    auto client = qobject_cast<win::x11::window*>(toplevel);
    if (client) {
        return true;
    }
    if (auto remnant = toplevel->remnant()) {
        return remnant->was_x11_client;
    }
    return false;
}

void window::performPaint(paint_type mask, QRegion region, WindowPaintData data)
{
    if (!(mask & (paint_type::window_transformed | paint_type::screen_transformed)))
        region &= win::visible_rect(toplevel);

    if (region.isEmpty())
        return;
    auto buffer = get_buffer<qpainter::buffer>();
    if (!buffer || !buffer->isValid()) {
        return;
    }
    if (!toplevel->damage().isEmpty()) {
        buffer->updateBuffer();
        toplevel->resetDamage();
    }

    QPainter* scenePainter = m_scene->scenePainter();
    QPainter* painter = scenePainter;
    painter->save();
    painter->setClipRegion(region);
    painter->setClipping(true);

    painter->translate(x(), y());
    if (flags(mask & paint_type::window_transformed)) {
        painter->translate(data.xTranslation(), data.yTranslation());
        painter->scale(data.xScale(), data.yScale());
    }

    const bool opaque = qFuzzyCompare(1.0, data.opacity());
    QImage tempImage;
    QPainter tempPainter;
    if (!opaque) {
        // need a temp render target which we later on blit to the screen
        tempImage = QImage(win::visible_rect(toplevel).size(), QImage::Format_ARGB32_Premultiplied);
        tempImage.fill(Qt::transparent);
        tempPainter.begin(&tempImage);
        tempPainter.save();
        tempPainter.translate(toplevel->frameGeometry().topLeft()
                              - win::visible_rect(toplevel).topLeft());
        painter = &tempPainter;
    }
    renderShadow(painter);
    renderWindowDecorations(painter);

    // render content
    QRectF source;
    QRectF target;
    QRectF viewportRectangle;
    if (toplevel->surface()) {
        viewportRectangle = toplevel->surface()->state().source_rectangle;
    }
    if (isXwaylandClient(toplevel)) {
        // special case for XWayland windows
        if (viewportRectangle.isValid()) {
            source = viewportRectangle;
            source.translate(win::frame_relative_client_rect(toplevel).topLeft());
        } else {
            source = win::frame_relative_client_rect(toplevel);
        }
        target = source;
    } else {
        if (viewportRectangle.isValid()) {
            const qreal imageScale = toplevel->bufferScale();
            source = QRectF(viewportRectangle.topLeft() * imageScale,
                            viewportRectangle.bottomRight() * imageScale);
        } else {
            source = buffer->image().rect();
        }
        target = win::render_geometry(toplevel).translated(-pos());
    }
    painter->drawImage(target, buffer->image(), source);

    if (!opaque) {
        tempPainter.restore();
        tempPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        QColor translucent(Qt::transparent);
        translucent.setAlphaF(data.opacity());
        tempPainter.fillRect(QRect(QPoint(0, 0), win::visible_rect(toplevel).size()), translucent);
        tempPainter.end();
        painter = scenePainter;
        painter->drawImage(
            win::visible_rect(toplevel).topLeft() - toplevel->frameGeometry().topLeft(), tempImage);
    }

    painter->restore();
}

void window::renderShadow(QPainter* painter)
{
    if (!win::shadow(toplevel)) {
        return;
    }
    auto shadow = static_cast<qpainter::shadow*>(win::shadow(toplevel));

    const QImage& shadowTexture = shadow->shadowTexture();
    const WindowQuadList& shadowQuads = shadow->shadowQuads();

    for (const auto& q : shadowQuads) {
        auto topLeft = q[0];
        auto bottomRight = q[2];
        QRectF target(
            topLeft.x(), topLeft.y(), bottomRight.x() - topLeft.x(), bottomRight.y() - topLeft.y());
        QRectF source(topLeft.textureX(),
                      topLeft.textureY(),
                      bottomRight.textureX() - topLeft.textureX(),
                      bottomRight.textureY() - topLeft.textureY());
        painter->drawImage(target, shadowTexture, source);
    }
}

void window::renderWindowDecorations(QPainter* painter)
{
    // TODO: custom decoration opacity
    auto const& ctrl = toplevel->control;
    auto remnant = toplevel->remnant();
    if (!ctrl && !remnant) {
        return;
    }

    bool noBorder = true;
    deco_renderer const* renderer = nullptr;
    QRect dtr, dlr, drr, dbr;

    if (ctrl && !toplevel->noBorder()) {
        if (win::decoration(toplevel)) {
            if (auto r = static_cast<deco_renderer*>(ctrl->deco().client->renderer())) {
                r->render();
                renderer = r;
            }
        }
        toplevel->layoutDecorationRects(dlr, dtr, drr, dbr);
        noBorder = false;
    } else if (remnant && !remnant->no_border) {
        noBorder = false;
        remnant->layout_decoration_rects(dlr, dtr, drr, dbr);
        renderer = static_cast<const deco_renderer*>(remnant->decoration_renderer);
    }
    if (noBorder || !renderer) {
        return;
    }

    painter->drawImage(dtr, renderer->image(deco_renderer::DecorationPart::Top));
    painter->drawImage(dlr, renderer->image(deco_renderer::DecorationPart::Left));
    painter->drawImage(drr, renderer->image(deco_renderer::DecorationPart::Right));
    painter->drawImage(dbr, renderer->image(deco_renderer::DecorationPart::Bottom));
}

render::buffer* window::create_buffer()
{
    return new buffer(this);
}

}
