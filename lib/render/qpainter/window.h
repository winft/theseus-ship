/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "buffer.h"
#include "deco_renderer.h"
#include "shadow.h"

#include "win/scene.h"

namespace KWin::render::qpainter
{

template<typename RefWin, typename Scene>
class window : public Scene::window_t
{
public:
    using window_t = typename Scene::window_t;
    using buffer_t = typename Scene::buffer_t;

    window(RefWin ref_win, Scene& scene)
        : window_t(ref_win, scene.platform)
        , scene{scene}
    {
    }

    void performPaint(paint_type mask, effect::window_paint_data& data) override
    {
        std::visit(overload{[&](auto&& win) { perform_paint(*win, mask, data); }}, *this->ref_win);
    }

protected:
    render::buffer<window_t>* create_buffer() override
    {
        return new buffer_t(this);
    }

private:
    template<typename Win>
    void perform_paint(Win& win, paint_type mask, effect::window_paint_data& data)
    {
        if (!(mask & (paint_type::window_transformed | paint_type::screen_transformed))) {
            data.paint.region &= win::visible_rect(&win);
        }

        if (data.paint.region.isEmpty()) {
            return;
        }

        auto buffer = this->template get_buffer<buffer_t>();
        if (!buffer || !buffer->isValid()) {
            return;
        }

        if (!win.render_data.damage_region.isEmpty()) {
            buffer->updateBuffer();
            win.render_data.damage_region = {};
        }

        auto scenePainter = scene.scenePainter();
        auto painter = scenePainter;
        painter->save();
        painter->setClipRegion(data.paint.region);
        painter->setClipping(true);

        auto const win_pos = win.geo.pos();
        painter->translate(win_pos.x(), win_pos.y());

        if (flags(mask & paint_type::window_transformed)) {
            painter->translate(data.paint.geo.translation.x(), data.paint.geo.translation.y());
            painter->scale(data.paint.geo.scale.x(), data.paint.geo.scale.y());
        }

        auto const opaque = qFuzzyCompare(1.0, data.paint.opacity);
        QImage tempImage;
        QPainter tempPainter;

        if (!opaque) {
            // need a temp render target which we later on blit to the screen
            tempImage = QImage(win::visible_rect(&win).size(), QImage::Format_ARGB32_Premultiplied);
            tempImage.fill(Qt::transparent);
            tempPainter.begin(&tempImage);
            tempPainter.save();
            tempPainter.translate(win.geo.frame.topLeft() - win::visible_rect(&win).topLeft());
            painter = &tempPainter;
        }

        render_shadow(win, painter);
        render_decorations(win, painter);

        // render content
        QRectF source;
        QRectF target;
        QRectF viewportRectangle;

        if constexpr (requires(Win win) { win.surface; }) {
            if (win.surface) {
                viewportRectangle = win.surface->state().source_rectangle;
            }
        }

        if constexpr (requires(Win win) { win.xcb_windows; }) {
            // special case for XWayland windows
            if (viewportRectangle.isValid()) {
                source = viewportRectangle;
                source.translate(win::frame_relative_client_rect(&win).topLeft());
            } else {
                source = win::frame_relative_client_rect(&win);
            }
            target = source;
        } else {
            if (viewportRectangle.isValid()) {
                const qreal imageScale = win.bufferScale();
                source = QRectF(viewportRectangle.topLeft() * imageScale,
                                viewportRectangle.bottomRight() * imageScale);
            } else {
                source = buffer->image.rect();
            }
            target = win::render_geometry(&win).translated(-win.geo.pos());
        }

        painter->drawImage(target, buffer->image, source);

        if (!opaque) {
            tempPainter.restore();
            tempPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            QColor translucent(Qt::transparent);
            translucent.setAlphaF(data.paint.opacity);
            tempPainter.fillRect(QRect(QPoint(0, 0), win::visible_rect(&win).size()), translucent);
            tempPainter.end();
            painter = scenePainter;
            painter->drawImage(win::visible_rect(&win).topLeft() - win.geo.frame.topLeft(),
                               tempImage);
        }

        painter->restore();
    }

    template<typename Win>
    void render_shadow(Win& win, QPainter* painter)
    {
        if (!win::shadow(&win)) {
            return;
        }
        auto shadow = static_cast<qpainter::shadow<window_t>*>(win::shadow(&win));

        const QImage& shadowTexture = shadow->shadowTexture();
        const WindowQuadList& shadowQuads = shadow->shadowQuads();

        for (const auto& q : shadowQuads) {
            auto topLeft = q[0];
            auto bottomRight = q[2];
            QRectF target(topLeft.x(),
                          topLeft.y(),
                          bottomRight.x() - topLeft.x(),
                          bottomRight.y() - topLeft.y());
            QRectF source(topLeft.textureX(),
                          topLeft.textureY(),
                          bottomRight.textureX() - topLeft.textureX(),
                          bottomRight.textureY() - topLeft.textureY());
            painter->drawImage(target, shadowTexture, source);
        }
    }

    template<typename Win>
    void render_decorations(Win& win, QPainter* painter)
    {
        // TODO: custom decoration opacity
        auto const& ctrl = win.control;
        auto& remnant = win.remnant;
        if (!ctrl && !remnant) {
            return;
        }

        bool noBorder = true;
        deco_render_data const* deco_data = nullptr;
        QRect dtr, dlr, drr, dbr;

        if (ctrl && !win.noBorder()) {
            if (win::decoration(&win)) {
                if (auto r
                    = static_cast<deco_renderer*>(ctrl->deco.client->renderer()->injector.get())) {
                    r->render();
                    deco_data = static_cast<deco_render_data const*>(r->data.get());
                }
            }
            win.layoutDecorationRects(dlr, dtr, drr, dbr);
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

    Scene& scene;
};

}
