/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco_renderer.h"
#include "shadow.h"

#include "render/window.h"

#include <kwineffects/paint_clipper.h>
#include <kwinxrender/utils.h>

#include <QPoint>
#include <QRect>
#include <QRegion>
#include <xcb/render.h>

namespace KWin::render::xrender
{

#define DOUBLE_TO_FIXED(d) ((xcb_render_fixed_t)((d)*65536))
#define FIXED_TO_DOUBLE(f) ((double)((f) / 65536.0))

template<typename RefWin, typename Scene>
class window : public Scene::window_t
{
public:
    using window_t = typename Scene::window_t;
    using buffer_t = typename Scene::buffer_t;

    window(RefWin ref_win, Scene& scene)
        : window_t(ref_win, *scene.platform.compositor)
        , scene{scene}
    {
        format = XRenderUtils::findPictFormat(
            std::visit(overload{[&](auto&& win) { return win->xcb_visual; }}, ref_win));
    }

    void performPaint(paint_type mask, QRegion region, WindowPaintData data) override
    {
        std::visit(overload{[&](auto&& win) { perform_paint(*win, mask, region, data); }},
                   *this->ref_win);
    }

    QRegion transformedShape() const
    {
        return transformed_shape;
    }

    void setTransformedShape(QRegion const& shape)
    {
        transformed_shape = shape;
    }

protected:
    render::buffer<window_t>* create_buffer() override
    {
        return new buffer_t(this, format);
    }

private:
    template<typename Win>
    void perform_paint(Win& win, paint_type mask, QRegion region, WindowPaintData data)
    {
        auto& temp_visibleRect = scene.temp_visible_rect;
        auto& s_tempPicture = scene.temp_picture;
        auto& s_fadeAlphaPicture = scene.fade_alpha_picture;

        // maybe nothing will be painted
        setTransformedShape(QRegion());

        // check if there is something to paint
        auto opaque = this->isOpaque() && qFuzzyCompare(data.opacity(), 1.0);

        /* HACK: It seems this causes painting glitches, disable temporarily
        if (( mask & paint_type::window_opaque ) ^ ( mask & scene::PAINT_WINDOW_TRANSLUCENT ))
            { // We are only painting either opaque OR translucent windows, not both
            if ( mask & paint_type::window_opaque && !opaque )
                return; // Only painting opaque and window is translucent
            if ( mask & scene::PAINT_WINDOW_TRANSLUCENT && opaque )
                return; // Only painting translucent and window is opaque
            }*/

        // Intersect the clip region with the rectangle the window occupies on the screen
        if (!(mask & (paint_type::window_transformed | paint_type::screen_transformed))) {
            region &= win::visible_rect(&win);
        }

        if (region.isEmpty()) {
            return;
        }

        auto pixmap = this->template get_buffer<buffer_t>();
        if (!pixmap || !pixmap->isValid()) {
            return;
        }

        xcb_render_picture_t pic = pixmap->picture;
        if (pic == XCB_RENDER_PICTURE_NONE) {
            // The render format can be null for GL and/or Xv visuals
            return;
        }

        win.render_data.damage_region = {};

        // set picture filter
        this->filter = image_filter_type::fast;

        // do required transformations
        auto const win_size = win.geo.size();
        auto const wr
            = mapToScreen(win, mask, data, QRect(0, 0, win_size.width(), win_size.height()));

        // Content rect (in the buffer)
        auto cr = win::frame_relative_client_rect(&win);
        qreal xscale = 1;
        qreal yscale = 1;
        bool scaled = false;

        auto const decorationRect = QRect(QPoint(), win.geo.size());

        if ((win.control && !win.noBorder()) || (win.remnant && !win.remnant->data.no_border)) {
            // decorated window
            transformed_shape = decorationRect;
            if (win.is_shape) {
                // "xeyes" + decoration
                transformed_shape -= bufferToWindowRect(cr);
                transformed_shape += bufferToWindowRegion(win.render_region());
            }
        } else {
            transformed_shape = bufferToWindowRegion(win.render_region());
        }

        if (auto shadow = win::shadow(&win)) {
            transformed_shape |= shadow->shadowRegion();
        }

        xcb_render_transform_t xform = {DOUBLE_TO_FIXED(1),
                                        DOUBLE_TO_FIXED(0),
                                        DOUBLE_TO_FIXED(0),
                                        DOUBLE_TO_FIXED(0),
                                        DOUBLE_TO_FIXED(1),
                                        DOUBLE_TO_FIXED(0),
                                        DOUBLE_TO_FIXED(0),
                                        DOUBLE_TO_FIXED(0),
                                        DOUBLE_TO_FIXED(1)};
        static const xcb_render_transform_t identity = {DOUBLE_TO_FIXED(1),
                                                        DOUBLE_TO_FIXED(0),
                                                        DOUBLE_TO_FIXED(0),
                                                        DOUBLE_TO_FIXED(0),
                                                        DOUBLE_TO_FIXED(1),
                                                        DOUBLE_TO_FIXED(0),
                                                        DOUBLE_TO_FIXED(0),
                                                        DOUBLE_TO_FIXED(0),
                                                        DOUBLE_TO_FIXED(1)};

        if (flags(mask & paint_type::window_transformed)) {
            xscale = data.xScale();
            yscale = data.yScale();
        }
        if (flags(mask & paint_type::screen_transformed)) {
            auto& screen_paint = scene.screen_paint;
            xscale *= screen_paint.xScale();
            yscale *= screen_paint.yScale();
        }

        if (!qFuzzyCompare(xscale, 1.0) || !qFuzzyCompare(yscale, 1.0)) {
            scaled = true;
            xform.matrix11 = DOUBLE_TO_FIXED(1.0 / xscale);
            xform.matrix22 = DOUBLE_TO_FIXED(1.0 / yscale);

            // transform the shape for clipping in paintTransformedScreen()
            QVector<QRect> rects;
            rects.reserve(transformed_shape.rectCount());
            for (const QRect& rect : transformed_shape) {
                const QRect transformedRect(qRound(rect.x() * xscale),
                                            qRound(rect.y() * yscale),
                                            qRound(rect.width() * xscale),
                                            qRound(rect.height() * yscale));
                rects.append(transformedRect);
            }
            transformed_shape.setRects(rects.constData(), rects.count());
        }

        transformed_shape.translate(mapToScreen(win, mask, data, QPoint(0, 0)));

        // clip by the region to paint
        PaintClipper pcreg(region);

        // clip by window's shape
        PaintClipper pc(transformed_shape);

        const bool wantShadow = this->m_shadow && !this->m_shadow->shadowRegion().isEmpty();

        // In order to obtain a pixel perfect rescaling
        // we need to blit the window content togheter with
        // decorations in a temporary pixmap and scale
        // the temporary pixmap at the end.
        // We should do this only if there is scaling and
        // the window has border
        // This solves a number of glitches and on top of this
        // it optimizes painting quite a bit
        auto const blitInTempPixmap = xRenderOffscreen()
            || (data.crossFadeProgress() < 1.0 && !opaque)
            || (scaled
                && (wantShadow || !win.noBorder()
                    || (win.remnant && !win.remnant->data.no_border)));

        auto renderTarget = scene.xrenderBufferPicture();

        if (blitInTempPixmap) {
            if (scene_xRenderOffscreenTarget()) {
                temp_visibleRect = win::visible_rect(&win).translated(-win.geo.pos());
                renderTarget = *scene_xRenderOffscreenTarget();
            } else {
                prepare_temp_pixmap(win);
                renderTarget = *s_tempPicture;
            }
        } else {
            xcb_render_set_picture_transform(connection(), pic, xform);
            if (this->filter == image_filter_type::good) {
                setPictureFilter(pic, image_filter_type::good);
            }

            // BEGIN OF STUPID RADEON HACK
            // This is needed to avoid hitting a fallback in the radeon driver.
            // The Render specification states that sampling pixels outside the
            // source picture results in alpha=0 pixels. This can be achieved by
            // setting the border color to transparent black, but since the border
            // color has the same format as the texture, it only works when the
            // texture has an alpha channel. So the driver falls back to software
            // when the repeat mode is RepeatNone, the picture has a non-identity
            // transformation matrix, and doesn't have an alpha channel.
            // Since we only scale the picture, we can work around this by setting
            // the repeat mode to RepeatPad.
            if (!win::has_alpha(win)) {
                const uint32_t values[] = {XCB_RENDER_REPEAT_PAD};
                xcb_render_change_picture(connection(), pic, XCB_RENDER_CP_REPEAT, values);
            }
            // END OF STUPID RADEON HACK
        }

#define MAP_RECT_TO_TARGET(_RECT_)                                                                 \
    if (blitInTempPixmap)                                                                          \
        _RECT_.translate(-temp_visibleRect.topLeft());                                             \
    else                                                                                           \
        _RECT_ = mapToScreen(win, mask, data, _RECT_)

        // BEGIN deco preparations
        bool noBorder = true;
        xcb_render_picture_t left = XCB_RENDER_PICTURE_NONE;
        xcb_render_picture_t top = XCB_RENDER_PICTURE_NONE;
        xcb_render_picture_t right = XCB_RENDER_PICTURE_NONE;
        xcb_render_picture_t bottom = XCB_RENDER_PICTURE_NONE;
        QRect dtr, dlr, drr, dbr;

        deco_render_data const* deco_data = nullptr;
        if (win.control && !win.noBorder()) {
            if (win::decoration(&win)) {
                if (auto deco_render = static_cast<deco_renderer*>(
                        win.control->deco.client->renderer()->injector.get())) {
                    deco_render->render();
                    deco_data = static_cast<deco_render_data const*>(deco_render->data.get());
                }
            }
            noBorder = win.noBorder();
            win.layoutDecorationRects(dlr, dtr, drr, dbr);
        }

        if (win.remnant && !win.remnant->data.no_border) {
            deco_data = static_cast<deco_render_data const*>(win.remnant->data.deco_render.get());
            noBorder = win.remnant->data.no_border;
            win.remnant->data.layout_decoration_rects(dlr, dtr, drr, dbr);
        }
        if (deco_data) {
            left = deco_data->picture(DecorationPart::Left);
            top = deco_data->picture(DecorationPart::Top);
            right = deco_data->picture(DecorationPart::Right);
            bottom = deco_data->picture(DecorationPart::Bottom);
        }
        if (!noBorder) {
            MAP_RECT_TO_TARGET(dtr);
            MAP_RECT_TO_TARGET(dlr);
            MAP_RECT_TO_TARGET(drr);
            MAP_RECT_TO_TARGET(dbr);
        }
        // END deco preparations

        // BEGIN shadow preparations
        QRect stlr, str, strr, srr, sbrr, sbr, sblr, slr;
        auto m_xrenderShadow = static_cast<xrender::shadow<window_t>*>(this->m_shadow.get());

        if (wantShadow) {
            m_xrenderShadow->layoutShadowRects(str, strr, srr, sbrr, sbr, sblr, slr, stlr);
            MAP_RECT_TO_TARGET(stlr);
            MAP_RECT_TO_TARGET(str);
            MAP_RECT_TO_TARGET(strr);
            MAP_RECT_TO_TARGET(srr);
            MAP_RECT_TO_TARGET(sbrr);
            MAP_RECT_TO_TARGET(sbr);
            MAP_RECT_TO_TARGET(sblr);
            MAP_RECT_TO_TARGET(slr);
        }
        // BEGIN end preparations

        // BEGIN client preparations
        QRect dr = cr;
        if (blitInTempPixmap) {
            dr.translate(-temp_visibleRect.topLeft());
        } else {
            // Destination rect
            dr = mapToScreen(win, mask, data, bufferToWindowRect(dr));
            if (scaled) {
                cr.moveLeft(cr.x() * xscale);
                cr.moveTop(cr.y() * yscale);
            }
        }

        const int clientRenderOp
            = (opaque || blitInTempPixmap) ? XCB_RENDER_PICT_OP_SRC : XCB_RENDER_PICT_OP_OVER;
        // END client preparations

#undef MAP_RECT_TO_TARGET

        for (PaintClipper::Iterator iterator; !iterator.isDone(); iterator.next()) {

#define RENDER_SHADOW_TILE(_TILE_, _RECT_)                                                         \
    xcb_render_composite(connection(),                                                             \
                         XCB_RENDER_PICT_OP_OVER,                                                  \
                         m_xrenderShadow->picture(shadow_element::_TILE_),                         \
                         shadowAlpha,                                                              \
                         renderTarget,                                                             \
                         0,                                                                        \
                         0,                                                                        \
                         0,                                                                        \
                         0,                                                                        \
                         _RECT_.x(),                                                               \
                         _RECT_.y(),                                                               \
                         _RECT_.width(),                                                           \
                         _RECT_.height())

            // shadow
            if (wantShadow) {
                xcb_render_picture_t shadowAlpha = XCB_RENDER_PICTURE_NONE;
                if (!opaque) {
                    shadowAlpha = xRenderBlendPicture(data.opacity());
                }
                RENDER_SHADOW_TILE(top_left, stlr);
                RENDER_SHADOW_TILE(top, str);
                RENDER_SHADOW_TILE(top_right, strr);
                RENDER_SHADOW_TILE(left, slr);
                RENDER_SHADOW_TILE(right, srr);
                RENDER_SHADOW_TILE(bottom_left, sblr);
                RENDER_SHADOW_TILE(bottom, sbr);
                RENDER_SHADOW_TILE(bottom_right, sbrr);
            }
#undef RENDER_SHADOW_TILE

            // Paint the window contents
            xcb_render_picture_t clientAlpha = XCB_RENDER_PICTURE_NONE;

            if (!opaque) {
                clientAlpha = xRenderBlendPicture(data.opacity());
            }

            xcb_render_composite(connection(),
                                 clientRenderOp,
                                 pic,
                                 clientAlpha,
                                 renderTarget,
                                 cr.x(),
                                 cr.y(),
                                 0,
                                 0,
                                 dr.x(),
                                 dr.y(),
                                 dr.width(),
                                 dr.height());
            if (data.crossFadeProgress() < 1.0 && data.crossFadeProgress() > 0.0) {
                auto previous = this->template previous_buffer<buffer_t>();
                if (previous && previous != pixmap) {
                    static xcb_render_color_t cFadeColor = {0, 0, 0, 0};
                    cFadeColor.alpha = uint16_t((1.0 - data.crossFadeProgress()) * 0xffff);
                    if (!s_fadeAlphaPicture) {
                        s_fadeAlphaPicture = new XRenderPicture(xRenderFill(cFadeColor));
                    } else {
                        xcb_rectangle_t rect = {0, 0, 1, 1};
                        xcb_render_fill_rectangles(connection(),
                                                   XCB_RENDER_PICT_OP_SRC,
                                                   *s_fadeAlphaPicture,
                                                   cFadeColor,
                                                   1,
                                                   &rect);
                    }

                    auto const previous_size = previous->win_integration->get_size();
                    auto const current_size = pixmap->win_integration->get_size();

                    if (previous_size != current_size) {
                        xcb_render_transform_t xform2
                            = {DOUBLE_TO_FIXED(FIXED_TO_DOUBLE(xform.matrix11)
                                               * previous_size.width() / current_size.width()),
                               DOUBLE_TO_FIXED(0),
                               DOUBLE_TO_FIXED(0),
                               DOUBLE_TO_FIXED(0),
                               DOUBLE_TO_FIXED(FIXED_TO_DOUBLE(xform.matrix22)
                                               * previous_size.height() / current_size.height()),
                               DOUBLE_TO_FIXED(0),
                               DOUBLE_TO_FIXED(0),
                               DOUBLE_TO_FIXED(0),
                               DOUBLE_TO_FIXED(1)};
                        xcb_render_set_picture_transform(connection(), previous->picture, xform2);
                    }

                    xcb_render_composite(connection(),
                                         opaque ? XCB_RENDER_PICT_OP_OVER : XCB_RENDER_PICT_OP_ATOP,
                                         previous->picture,
                                         *s_fadeAlphaPicture,
                                         renderTarget,
                                         cr.x(),
                                         cr.y(),
                                         0,
                                         0,
                                         dr.x(),
                                         dr.y(),
                                         dr.width(),
                                         dr.height());

                    if (previous_size != current_size) {
                        xcb_render_set_picture_transform(connection(), previous->picture, identity);
                    }
                }
            }

            if (!opaque) {
                transformed_shape = QRegion();
            }

            if (!noBorder) {
                xcb_render_picture_t decorationAlpha = xRenderBlendPicture(data.opacity());
                auto renderDeco = [decorationAlpha, renderTarget](xcb_render_picture_t deco,
                                                                  const QRect& rect) {
                    if (deco == XCB_RENDER_PICTURE_NONE) {
                        return;
                    }
                    xcb_render_composite(connection(),
                                         XCB_RENDER_PICT_OP_OVER,
                                         deco,
                                         decorationAlpha,
                                         renderTarget,
                                         0,
                                         0,
                                         0,
                                         0,
                                         rect.x(),
                                         rect.y(),
                                         rect.width(),
                                         rect.height());
                };
                renderDeco(top, dtr);
                renderDeco(left, dlr);
                renderDeco(right, drr);
                renderDeco(bottom, dbr);
            }

            if (data.brightness() != 1.0) {
                // fake brightness change by overlaying black
                const float alpha = (1 - data.brightness()) * data.opacity();
                xcb_rectangle_t rect;
                if (blitInTempPixmap) {
                    rect.x = -temp_visibleRect.left();
                    rect.y = -temp_visibleRect.top();
                    auto const size = win.geo.size();
                    rect.width = size.width();
                    rect.height = size.height();
                } else {
                    rect.x = wr.x();
                    rect.y = wr.y();
                    rect.width = wr.width();
                    rect.height = wr.height();
                }
                xcb_render_fill_rectangles(connection(),
                                           XCB_RENDER_PICT_OP_OVER,
                                           renderTarget,
                                           preMultiply(data.brightness() < 1.0
                                                           ? QColor(0, 0, 0, 255 * alpha)
                                                           : QColor(255, 255, 255, -alpha * 255)),
                                           1,
                                           &rect);
            }
            if (blitInTempPixmap) {
                const QRect r = mapToScreen(win, mask, data, temp_visibleRect);
                xcb_render_set_picture_transform(connection(), *s_tempPicture, xform);
                setPictureFilter(*s_tempPicture, this->filter);
                xcb_render_composite(connection(),
                                     XCB_RENDER_PICT_OP_OVER,
                                     *s_tempPicture,
                                     XCB_RENDER_PICTURE_NONE,
                                     scene.xrenderBufferPicture(),
                                     0,
                                     0,
                                     0,
                                     0,
                                     r.x(),
                                     r.y(),
                                     r.width(),
                                     r.height());
                xcb_render_set_picture_transform(connection(), *s_tempPicture, identity);
            }
        }

        if (scaled && !blitInTempPixmap) {
            xcb_render_set_picture_transform(connection(), pic, identity);
            if (this->filter == image_filter_type::good)
                setPictureFilter(pic, image_filter_type::fast);
            if (!win::has_alpha(win)) {
                const uint32_t values[] = {XCB_RENDER_REPEAT_NONE};
                xcb_render_change_picture(connection(), pic, XCB_RENDER_CP_REPEAT, values);
            }
        }

        if (xRenderOffscreen()) {
            scene_setXRenderOffscreenTarget(*s_tempPicture);
        }
    }

    template<typename Win>
    QRect mapToScreen(Win const& win,
                      paint_type mask,
                      const WindowPaintData& data,
                      const QRect& rect) const
    {
        QRect r = rect;

        if (flags(mask & paint_type::window_transformed)) {
            // Apply the window transformation
            r.moveTo(r.x() * data.xScale() + data.xTranslation(),
                     r.y() * data.yScale() + data.yTranslation());
            r.setWidth(r.width() * data.xScale());
            r.setHeight(r.height() * data.yScale());
        }

        // Move the rectangle to the screen position
        auto const win_pos = win.geo.pos();
        r.translate(win_pos.x(), win_pos.y());

        if (flags(mask & paint_type::screen_transformed)) {
            // Apply the screen transformation
            auto& screen_paint = scene.screen_paint;
            r.moveTo(r.x() * screen_paint.xScale() + screen_paint.xTranslation(),
                     r.y() * screen_paint.yScale() + screen_paint.yTranslation());
            r.setWidth(r.width() * screen_paint.xScale());
            r.setHeight(r.height() * screen_paint.yScale());
        }

        return r;
    }

    template<typename Win>
    QPoint mapToScreen(Win const& win,
                       paint_type mask,
                       const WindowPaintData& data,
                       const QPoint& point) const
    {
        QPoint pt = point;

        if (flags(mask & paint_type::window_transformed)) {
            // Apply the window transformation
            pt.rx() = pt.x() * data.xScale() + data.xTranslation();
            pt.ry() = pt.y() * data.yScale() + data.yTranslation();
        }

        // Move the point to the screen position
        auto const win_pos = win.geo.pos();
        pt += QPoint(win_pos.x(), win_pos.y());

        if (flags(mask & paint_type::screen_transformed)) {
            // Apply the screen transformation
            auto& screen_paint = scene.screen_paint;
            pt.rx() = pt.x() * screen_paint.xScale() + screen_paint.xTranslation();
            pt.ry() = pt.y() * screen_paint.yScale() + screen_paint.yTranslation();
        }

        return pt;
    }

    QRect bufferToWindowRect(const QRect& rect) const
    {
        return rect.translated(this->bufferOffset());
    }

    QRegion bufferToWindowRegion(const QRegion& region) const
    {
        return region.translated(this->bufferOffset());
    }

    template<typename Win>
    void prepare_temp_pixmap(Win const& win)
    {
        auto& temp_visibleRect = scene.temp_visible_rect;
        auto& s_tempPicture = scene.temp_picture;

        auto const oldSize = temp_visibleRect.size();
        temp_visibleRect = win::visible_rect(&win).translated(-win.geo.pos());

        if (s_tempPicture
            && (oldSize.width() < temp_visibleRect.width()
                || oldSize.height() < temp_visibleRect.height())) {
            delete s_tempPicture;
            s_tempPicture = nullptr;

            // invalidate, better crash than cause weird results for developers
            scene_setXRenderOffscreenTarget(0);
        }

        if (!s_tempPicture) {
            xcb_pixmap_t pix = xcb_generate_id(connection());
            xcb_create_pixmap(connection(),
                              32,
                              pix,
                              rootWindow(),
                              temp_visibleRect.width(),
                              temp_visibleRect.height());
            s_tempPicture = new XRenderPicture(pix, 32);
            xcb_free_pixmap(connection(), pix);
        }

        xcb_render_color_t const transparent = {0, 0, 0, 0};
        xcb_rectangle_t const rect
            = {0, 0, uint16_t(temp_visibleRect.width()), uint16_t(temp_visibleRect.height())};
        xcb_render_fill_rectangles(
            connection(), XCB_RENDER_PICT_OP_SRC, *s_tempPicture, transparent, 1, &rect);
    }

    void setPictureFilter(xcb_render_picture_t pic, image_filter_type filter)
    {
        QByteArray filterName;
        switch (filter) {
        case image_filter_type::fast:
            filterName = QByteArray("fast");
            break;
        case image_filter_type::good:
            filterName = QByteArray("good");
            break;
        }
        xcb_render_set_picture_filter(
            connection(), pic, filterName.length(), filterName.constData(), 0, nullptr);
    }

    xcb_render_pictformat_t format;
    QRegion transformed_shape;
    Scene& scene;
};

}