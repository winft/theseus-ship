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

#include "win/deco/client_impl.h"
#include "win/geo.h"
#include "win/scene.h"
#include "win/x11/window.h"

#include <QPainter>
#include <Wrapland/Server/surface.h>

namespace KWin::render::qpainter
{

window::window(Toplevel* c, qpainter::scene& scene)
    : render::window(c, scene)
{
}

window::~window()
{
}

static bool isXwaylandClient(Toplevel* toplevel)
{
    auto client = dynamic_cast<win::x11::window*>(toplevel);
    if (client) {
        return true;
    }
    if (auto& remnant = toplevel->remnant) {
        return remnant->data.was_x11_client;
    }
    return false;
}

void window::performPaint(paint_type mask, QRegion region, WindowPaintData data)
{
    if (!(mask & (paint_type::window_transformed | paint_type::screen_transformed)))
        region &= win::visible_rect(ref_win);

    if (region.isEmpty())
        return;
    auto buffer = get_buffer<qpainter::buffer>();
    if (!buffer || !buffer->isValid()) {
        return;
    }
    if (!ref_win->damage_region.isEmpty()) {
        buffer->updateBuffer();
        ref_win->resetDamage();
    }

    auto scenePainter = static_cast<qpainter::scene&>(scene).scenePainter();
    QPainter* painter = scenePainter;
    painter->save();
    painter->setClipRegion(region);
    painter->setClipping(true);

    auto const win_pos = ref_win->pos();
    painter->translate(win_pos.x(), win_pos.y());
    if (flags(mask & paint_type::window_transformed)) {
        painter->translate(data.xTranslation(), data.yTranslation());
        painter->scale(data.xScale(), data.yScale());
    }

    const bool opaque = qFuzzyCompare(1.0, data.opacity());
    QImage tempImage;
    QPainter tempPainter;
    if (!opaque) {
        // need a temp render target which we later on blit to the screen
        tempImage = QImage(win::visible_rect(ref_win).size(), QImage::Format_ARGB32_Premultiplied);
        tempImage.fill(Qt::transparent);
        tempPainter.begin(&tempImage);
        tempPainter.save();
        tempPainter.translate(ref_win->frameGeometry().topLeft()
                              - win::visible_rect(ref_win).topLeft());
        painter = &tempPainter;
    }
    renderShadow(painter);
    renderWindowDecorations(painter);

    // render content
    QRectF source;
    QRectF target;
    QRectF viewportRectangle;
    if (ref_win->surface) {
        viewportRectangle = ref_win->surface->state().source_rectangle;
    }
    if (isXwaylandClient(ref_win)) {
        // special case for XWayland windows
        if (viewportRectangle.isValid()) {
            source = viewportRectangle;
            source.translate(win::frame_relative_client_rect(ref_win).topLeft());
        } else {
            source = win::frame_relative_client_rect(ref_win);
        }
        target = source;
    } else {
        if (viewportRectangle.isValid()) {
            const qreal imageScale = ref_win->bufferScale();
            source = QRectF(viewportRectangle.topLeft() * imageScale,
                            viewportRectangle.bottomRight() * imageScale);
        } else {
            source = buffer->image().rect();
        }
        target = win::render_geometry(ref_win).translated(-ref_win->pos());
    }
    painter->drawImage(target, buffer->image(), source);

    if (!opaque) {
        tempPainter.restore();
        tempPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        QColor translucent(Qt::transparent);
        translucent.setAlphaF(data.opacity());
        tempPainter.fillRect(QRect(QPoint(0, 0), win::visible_rect(ref_win).size()), translucent);
        tempPainter.end();
        painter = scenePainter;
        painter->drawImage(
            win::visible_rect(ref_win).topLeft() - ref_win->frameGeometry().topLeft(), tempImage);
    }

    painter->restore();
}

void window::renderShadow(QPainter* painter)
{
    if (!win::shadow(ref_win)) {
        return;
    }
    auto shadow = static_cast<qpainter::shadow*>(win::shadow(ref_win));

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
    auto const& ctrl = ref_win->control;
    auto& remnant = ref_win->remnant;
    if (!ctrl && !remnant) {
        return;
    }

    bool noBorder = true;
    deco_render_data const* deco_data = nullptr;
    QRect dtr, dlr, drr, dbr;

    if (ctrl && !ref_win->noBorder()) {
        if (win::decoration(ref_win)) {
            if (auto r = static_cast<deco_renderer<win::deco::client_impl<Toplevel>>*>(
                    ctrl->deco.client->renderer())) {
                r->render();
                deco_data = static_cast<deco_render_data const*>(r->data.get());
            }
        }
        ref_win->layoutDecorationRects(dlr, dtr, drr, dbr);
        noBorder = false;
    } else if (remnant && !remnant->data.no_border) {
        noBorder = false;
        remnant->data.layout_decoration_rects(dlr, dtr, drr, dbr);
        deco_data = static_cast<deco_render_data const*>(remnant->data.deco_render.get());
    }
    if (noBorder || !deco_data) {
        return;
    }

    painter->drawImage(dtr, deco_data->image(DecorationPart::Top));
    painter->drawImage(dlr, deco_data->image(DecorationPart::Left));
    painter->drawImage(drr, deco_data->image(DecorationPart::Right));
    painter->drawImage(dbr, deco_data->image(DecorationPart::Bottom));
}

render::buffer* window::create_buffer()
{
    return new buffer(this);
}

}
