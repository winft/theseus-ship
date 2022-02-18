/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "backend.h"
#include "deco_renderer.h"
#include "scene.h"
#include "shadow.h"

#include "win/geo.h"
#include "win/x11/window.h"

#include "kwinxrenderutils.h"

namespace KWin::render::xrender
{

#define DOUBLE_TO_FIXED(d) ((xcb_render_fixed_t)((d)*65536))
#define FIXED_TO_DOUBLE(f) ((double)((f) / 65536.0))

XRenderPicture* window::s_tempPicture = nullptr;
QRect window::temp_visibleRect;
XRenderPicture* window::s_fadeAlphaPicture = nullptr;

window::window(Toplevel* c, xrender::scene* scene)
    : render::window(c)
    , m_scene(scene)
    , format(XRenderUtils::findPictFormat(c->visual()))
{
}

window::~window()
{
}

void window::cleanup()
{
    delete s_tempPicture;
    s_tempPicture = nullptr;
    delete s_fadeAlphaPicture;
    s_fadeAlphaPicture = nullptr;
}

// Maps window coordinates to screen coordinates
QRect window::mapToScreen(paint_type mask, const WindowPaintData& data, const QRect& rect) const
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
    r.translate(x(), y());

    if (flags(mask & paint_type::screen_transformed)) {
        // Apply the screen transformation
        r.moveTo(r.x() * scene::screen_paint.xScale() + scene::screen_paint.xTranslation(),
                 r.y() * scene::screen_paint.yScale() + scene::screen_paint.yTranslation());
        r.setWidth(r.width() * scene::screen_paint.xScale());
        r.setHeight(r.height() * scene::screen_paint.yScale());
    }

    return r;
}

// Maps window coordinates to screen coordinates
QPoint window::mapToScreen(paint_type mask, const WindowPaintData& data, const QPoint& point) const
{
    QPoint pt = point;

    if (flags(mask & paint_type::window_transformed)) {
        // Apply the window transformation
        pt.rx() = pt.x() * data.xScale() + data.xTranslation();
        pt.ry() = pt.y() * data.yScale() + data.yTranslation();
    }

    // Move the point to the screen position
    pt += QPoint(x(), y());

    if (flags(mask & paint_type::screen_transformed)) {
        // Apply the screen transformation
        pt.rx() = pt.x() * scene::screen_paint.xScale() + scene::screen_paint.xTranslation();
        pt.ry() = pt.y() * scene::screen_paint.yScale() + scene::screen_paint.yTranslation();
    }

    return pt;
}

QRect window::bufferToWindowRect(const QRect& rect) const
{
    return rect.translated(bufferOffset());
}

QRegion window::bufferToWindowRegion(const QRegion& region) const
{
    return region.translated(bufferOffset());
}

void window::prepareTempPixmap()
{
    const QSize oldSize = temp_visibleRect.size();
    temp_visibleRect = win::visible_rect(toplevel).translated(-toplevel->pos());
    if (s_tempPicture
        && (oldSize.width() < temp_visibleRect.width()
            || oldSize.height() < temp_visibleRect.height())) {
        delete s_tempPicture;
        s_tempPicture = nullptr;
        scene_setXRenderOffscreenTarget(
            0); // invalidate, better crash than cause weird results for developers
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
    const xcb_render_color_t transparent = {0, 0, 0, 0};
    const xcb_rectangle_t rect
        = {0, 0, uint16_t(temp_visibleRect.width()), uint16_t(temp_visibleRect.height())};
    xcb_render_fill_rectangles(
        connection(), XCB_RENDER_PICT_OP_SRC, *s_tempPicture, transparent, 1, &rect);
}

// paint the window
void window::performPaint(paint_type mask, QRegion region, WindowPaintData data)
{
    setTransformedShape(QRegion()); // maybe nothing will be painted
    // check if there is something to paint
    bool opaque = isOpaque() && qFuzzyCompare(data.opacity(), 1.0);
    /* HACK: It seems this causes painting glitches, disable temporarily
    if (( mask & paint_type::window_opaque ) ^ ( mask & scene::PAINT_WINDOW_TRANSLUCENT ))
        { // We are only painting either opaque OR translucent windows, not both
        if ( mask & paint_type::window_opaque && !opaque )
            return; // Only painting opaque and window is translucent
        if ( mask & scene::PAINT_WINDOW_TRANSLUCENT && opaque )
            return; // Only painting translucent and window is opaque
        }*/
    // Intersect the clip region with the rectangle the window occupies on the screen
    if (!(mask & (paint_type::window_transformed | paint_type::screen_transformed)))
        region &= win::visible_rect(toplevel);

    if (region.isEmpty())
        return;
    auto pixmap = windowPixmap<window_pixmap>();
    if (!pixmap || !pixmap->isValid()) {
        return;
    }
    xcb_render_picture_t pic = pixmap->picture();
    if (pic == XCB_RENDER_PICTURE_NONE) // The render format can be null for GL and/or Xv visuals
        return;
    toplevel->resetDamage();

    // set picture filter
    filter = image_filter_type::fast;

    // do required transformations
    const QRect wr = mapToScreen(mask, data, QRect(0, 0, width(), height()));

    // Content rect (in the buffer)
    auto cr = win::frame_relative_client_rect(toplevel);
    qreal xscale = 1;
    qreal yscale = 1;
    bool scaled = false;

    auto client = dynamic_cast<win::x11::window*>(toplevel);
    auto remnant = toplevel->remnant();
    auto const decorationRect = QRect(QPoint(), toplevel->size());
    if (((client && !client->noBorder()) || (remnant && !remnant->no_border)) && true) {
        // decorated client
        transformed_shape = decorationRect;
        if (toplevel->shape()) {
            // "xeyes" + decoration
            transformed_shape -= bufferToWindowRect(cr);
            transformed_shape += bufferToWindowRegion(get_window()->render_region());
        }
    } else {
        transformed_shape = bufferToWindowRegion(get_window()->render_region());
    }
    if (auto shadow = win::shadow(toplevel)) {
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
        xscale *= scene::screen_paint.xScale();
        yscale *= scene::screen_paint.yScale();
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

    transformed_shape.translate(mapToScreen(mask, data, QPoint(0, 0)));
    PaintClipper pcreg(region);         // clip by the region to paint
    PaintClipper pc(transformed_shape); // clip by window's shape

    const bool wantShadow = m_shadow && !m_shadow->shadowRegion().isEmpty();

    // In order to obtain a pixel perfect rescaling
    // we need to blit the window content togheter with
    // decorations in a temporary pixmap and scale
    // the temporary pixmap at the end.
    // We should do this only if there is scaling and
    // the window has border
    // This solves a number of glitches and on top of this
    // it optimizes painting quite a bit
    const bool blitInTempPixmap = xRenderOffscreen() || (data.crossFadeProgress() < 1.0 && !opaque)
        || (scaled
            && (wantShadow || (client && !client->noBorder()) || (remnant && !remnant->no_border)));

    xcb_render_picture_t renderTarget = m_scene->xrenderBufferPicture();
    if (blitInTempPixmap) {
        if (scene_xRenderOffscreenTarget()) {
            temp_visibleRect = win::visible_rect(toplevel).translated(-toplevel->pos());
            renderTarget = *scene_xRenderOffscreenTarget();
        } else {
            prepareTempPixmap();
            renderTarget = *s_tempPicture;
        }
    } else {
        xcb_render_set_picture_transform(connection(), pic, xform);
        if (filter == image_filter_type::good) {
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
        if (!get_window()->hasAlpha()) {
            const uint32_t values[] = {XCB_RENDER_REPEAT_PAD};
            xcb_render_change_picture(connection(), pic, XCB_RENDER_CP_REPEAT, values);
        }
        // END OF STUPID RADEON HACK
    }
#define MAP_RECT_TO_TARGET(_RECT_)                                                                 \
    if (blitInTempPixmap)                                                                          \
        _RECT_.translate(-temp_visibleRect.topLeft());                                             \
    else                                                                                           \
        _RECT_ = mapToScreen(mask, data, _RECT_)

    // BEGIN deco preparations
    bool noBorder = true;
    xcb_render_picture_t left = XCB_RENDER_PICTURE_NONE;
    xcb_render_picture_t top = XCB_RENDER_PICTURE_NONE;
    xcb_render_picture_t right = XCB_RENDER_PICTURE_NONE;
    xcb_render_picture_t bottom = XCB_RENDER_PICTURE_NONE;
    QRect dtr, dlr, drr, dbr;
    deco_renderer const* renderer = nullptr;
    if (client) {
        if (client && !client->noBorder()) {
            if (win::decoration(client)) {
                auto r = static_cast<deco_renderer*>(client->control->deco().client->renderer());
                if (r) {
                    r->render();
                    renderer = r;
                }
            }
            noBorder = client->noBorder();
            client->layoutDecorationRects(dlr, dtr, drr, dbr);
        }
    }
    if (remnant && !remnant->no_border) {
        renderer = static_cast<const deco_renderer*>(remnant->decoration_renderer);
        noBorder = remnant->no_border;
        remnant->layout_decoration_rects(dlr, dtr, drr, dbr);
    }
    if (renderer) {
        left = renderer->picture(deco_renderer::DecorationPart::Left);
        top = renderer->picture(deco_renderer::DecorationPart::Top);
        right = renderer->picture(deco_renderer::DecorationPart::Right);
        bottom = renderer->picture(deco_renderer::DecorationPart::Bottom);
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
    auto m_xrenderShadow = static_cast<xrender::shadow*>(m_shadow);

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
        dr = mapToScreen(mask, data, bufferToWindowRect(dr)); // Destination rect
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
            auto previous = previousWindowPixmap<window_pixmap>();
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
                if (previous->size() != pixmap->size()) {
                    xcb_render_transform_t xform2
                        = {DOUBLE_TO_FIXED(FIXED_TO_DOUBLE(xform.matrix11)
                                           * previous->size().width() / pixmap->size().width()),
                           DOUBLE_TO_FIXED(0),
                           DOUBLE_TO_FIXED(0),
                           DOUBLE_TO_FIXED(0),
                           DOUBLE_TO_FIXED(FIXED_TO_DOUBLE(xform.matrix22)
                                           * previous->size().height() / pixmap->size().height()),
                           DOUBLE_TO_FIXED(0),
                           DOUBLE_TO_FIXED(0),
                           DOUBLE_TO_FIXED(0),
                           DOUBLE_TO_FIXED(1)};
                    xcb_render_set_picture_transform(connection(), previous->picture(), xform2);
                }

                xcb_render_composite(connection(),
                                     opaque ? XCB_RENDER_PICT_OP_OVER : XCB_RENDER_PICT_OP_ATOP,
                                     previous->picture(),
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

                if (previous->size() != pixmap->size()) {
                    xcb_render_set_picture_transform(connection(), previous->picture(), identity);
                }
            }
        }
        if (!opaque)
            transformed_shape = QRegion();

        if (client || remnant) {
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
        }

        if (data.brightness() != 1.0) {
            // fake brightness change by overlaying black
            const float alpha = (1 - data.brightness()) * data.opacity();
            xcb_rectangle_t rect;
            if (blitInTempPixmap) {
                rect.x = -temp_visibleRect.left();
                rect.y = -temp_visibleRect.top();
                rect.width = width();
                rect.height = height();
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
            const QRect r = mapToScreen(mask, data, temp_visibleRect);
            xcb_render_set_picture_transform(connection(), *s_tempPicture, xform);
            setPictureFilter(*s_tempPicture, filter);
            xcb_render_composite(connection(),
                                 XCB_RENDER_PICT_OP_OVER,
                                 *s_tempPicture,
                                 XCB_RENDER_PICTURE_NONE,
                                 m_scene->xrenderBufferPicture(),
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
        if (filter == image_filter_type::good)
            setPictureFilter(pic, image_filter_type::fast);
        if (!get_window()->hasAlpha()) {
            const uint32_t values[] = {XCB_RENDER_REPEAT_NONE};
            xcb_render_change_picture(connection(), pic, XCB_RENDER_CP_REPEAT, values);
        }
    }
    if (xRenderOffscreen())
        scene_setXRenderOffscreenTarget(*s_tempPicture);
}

void window::setPictureFilter(xcb_render_picture_t pic, image_filter_type filter)
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

render::window_pixmap* window::createWindowPixmap()
{
    return new window_pixmap(this, format);
}

void scene::handle_screen_geometry_change(QSize const& size)
{
    m_backend->screenGeometryChanged(size);
}

xcb_render_picture_t scene::xrenderBufferPicture() const
{
    return m_backend->buffer();
}

QRegion window::transformedShape() const
{
    return transformed_shape;
}

void window::setTransformedShape(QRegion const& shape)
{
    transformed_shape = shape;
}

//****************************************
// window_pixmap
//****************************************

window_pixmap::window_pixmap(render::window* window, xcb_render_pictformat_t format)
    : render::window_pixmap(window)
    , m_picture(XCB_RENDER_PICTURE_NONE)
    , m_format(format)
{
}

window_pixmap::~window_pixmap()
{
    if (m_picture != XCB_RENDER_PICTURE_NONE) {
        xcb_render_free_picture(connection(), m_picture);
    }
}

void window_pixmap::create()
{
    if (isValid()) {
        return;
    }
    render::window_pixmap::create();
    if (!isValid()) {
        return;
    }
    m_picture = xcb_generate_id(connection());
    xcb_render_create_picture(connection(), m_picture, pixmap(), m_format, 0, nullptr);
}

xcb_render_picture_t window_pixmap::picture() const
{
    return m_picture;
}

}
