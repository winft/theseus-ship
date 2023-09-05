/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "non_composited_outline.h"

#include "xrender/utils.h"

#include <QColor>
#include <xcb/render.h>

namespace KWin::render::backend::x11
{

non_composited_outline::non_composited_outline(base::x11::data const& data,
                                               render::outline* outline)
    : outline_visual(outline)
    , m_initialized(false)
    , data{data}
{
}

non_composited_outline::~non_composited_outline()
{
}

void non_composited_outline::show()
{
    if (!m_initialized) {
        const QRect geo(0, 0, 1, 1);
        const uint32_t values[] = {true};
        // TODO: use template variant
        m_leftOutline.create(
            data.connection, data.root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
        m_rightOutline.create(
            data.connection, data.root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
        m_topOutline.create(
            data.connection, data.root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
        m_bottomOutline.create(
            data.connection, data.root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
        m_initialized = true;
    }

    auto const defaultDepth = base::x11::xcb::default_depth(data.connection, data.screen_number);

    const QRect& outlineGeometry = get_outline()->geometry();
    // left/right parts are between top/bottom, they don't reach as far as the corners
    const uint16_t verticalWidth = 5;
    const uint16_t verticalHeight = outlineGeometry.height() - 10;
    const uint16_t horizontalWidth = outlineGeometry.width();
    const uint horizontalHeight = 5;
    m_leftOutline.set_geometry(
        outlineGeometry.x(), outlineGeometry.y() + 5, verticalWidth, verticalHeight);
    m_rightOutline.set_geometry(outlineGeometry.x() + outlineGeometry.width() - 5,
                                outlineGeometry.y() + 5,
                                verticalWidth,
                                verticalHeight);
    m_topOutline.set_geometry(
        outlineGeometry.x(), outlineGeometry.y(), horizontalWidth, horizontalHeight);
    m_bottomOutline.set_geometry(outlineGeometry.x(),
                                 outlineGeometry.y() + outlineGeometry.height() - 5,
                                 horizontalWidth,
                                 horizontalHeight);

    const xcb_render_color_t white = {0xffff, 0xffff, 0xffff, 0xffff};
    QColor qGray(Qt::gray);
    const xcb_render_color_t gray = {uint16_t(0xffff * qGray.redF()),
                                     uint16_t(0xffff * qGray.greenF()),
                                     uint16_t(0xffff * qGray.blueF()),
                                     0xffff};
    const xcb_render_color_t black = {0, 0, 0, 0xffff};
    {
        xcb_pixmap_t xpix = xcb_generate_id(data.connection);
        xcb_create_pixmap(
            data.connection, defaultDepth, xpix, data.root_window, verticalWidth, verticalHeight);
        XRenderPicture pic(xpix, defaultDepth);

        xcb_rectangle_t rect = {0, 0, 5, verticalHeight};
        xcb_render_fill_rectangles(data.connection, XCB_RENDER_PICT_OP_SRC, pic, white, 1, &rect);
        rect.x = 1;
        rect.width = 3;
        xcb_render_fill_rectangles(data.connection, XCB_RENDER_PICT_OP_SRC, pic, gray, 1, &rect);
        rect.x = 2;
        rect.width = 1;
        xcb_render_fill_rectangles(data.connection, XCB_RENDER_PICT_OP_SRC, pic, black, 1, &rect);

        m_leftOutline.set_background_pixmap(xpix);
        m_rightOutline.set_background_pixmap(xpix);
        // According to the XSetWindowBackgroundPixmap documentation the pixmap can be freed.
        xcb_free_pixmap(data.connection, xpix);
    }
    {
        xcb_pixmap_t xpix = xcb_generate_id(data.connection);
        xcb_create_pixmap(data.connection,
                          defaultDepth,
                          xpix,
                          data.root_window,
                          horizontalWidth,
                          horizontalHeight);
        XRenderPicture pic(xpix, defaultDepth);

        xcb_rectangle_t rect = {0, 0, horizontalWidth, horizontalHeight};
        xcb_render_fill_rectangles(data.connection, XCB_RENDER_PICT_OP_SRC, pic, white, 1, &rect);
        xcb_rectangle_t grayRects[] = {{1, 1, uint16_t(horizontalWidth - 2), 3},
                                       {1, 4, 3, 1},
                                       {int16_t(horizontalWidth - 4), 4, 3, 1}};
        xcb_render_fill_rectangles(
            data.connection, XCB_RENDER_PICT_OP_SRC, pic, gray, 3, grayRects);
        xcb_rectangle_t blackRects[] = {{2, 2, uint16_t(horizontalWidth - 4), 1},
                                        {2, 3, 1, 2},
                                        {int16_t(horizontalWidth - 3), 3, 1, 2}};
        xcb_render_fill_rectangles(
            data.connection, XCB_RENDER_PICT_OP_SRC, pic, black, 3, blackRects);
        m_topOutline.set_background_pixmap(xpix);
        // According to the XSetWindowBackgroundPixmap documentation the pixmap can be freed.
        xcb_free_pixmap(data.connection, xpix);
    }
    {
        xcb_pixmap_t xpix = xcb_generate_id(data.connection);
        xcb_create_pixmap(
            data.connection, defaultDepth, xpix, data.root_window, outlineGeometry.width(), 5);
        XRenderPicture pic(xpix, defaultDepth);

        xcb_rectangle_t rect = {0, 0, horizontalWidth, horizontalHeight};
        xcb_render_fill_rectangles(data.connection, XCB_RENDER_PICT_OP_SRC, pic, white, 1, &rect);
        xcb_rectangle_t grayRects[] = {{1, 1, uint16_t(horizontalWidth - 2), 3},
                                       {1, 0, 3, 1},
                                       {int16_t(horizontalWidth - 4), 0, 3, 1}};
        xcb_render_fill_rectangles(
            data.connection, XCB_RENDER_PICT_OP_SRC, pic, gray, 3, grayRects);
        xcb_rectangle_t blackRects[] = {{2, 2, uint16_t(horizontalWidth - 4), 1},
                                        {2, 0, 1, 2},
                                        {int16_t(horizontalWidth - 3), 0, 1, 2}};
        xcb_render_fill_rectangles(
            data.connection, XCB_RENDER_PICT_OP_SRC, pic, black, 3, blackRects);
        m_bottomOutline.set_background_pixmap(xpix);
        // According to the XSetWindowBackgroundPixmap documentation the pixmap can be freed.
        xcb_free_pixmap(data.connection, xpix);
    }
    forEachWindow(&base::x11::xcb::window::clear);
    forEachWindow(&base::x11::xcb::window::map);
}

void non_composited_outline::hide()
{
    forEachWindow(&base::x11::xcb::window::unmap);
}

}
