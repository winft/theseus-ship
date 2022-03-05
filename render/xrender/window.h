/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/window.h"

#include <kwinxrender/utils.h>

#include <QPoint>
#include <QRect>
#include <QRegion>
#include <xcb/render.h>

namespace KWin::render::xrender
{

class scene;

class window : public render::window
{
public:
    window(Toplevel* c, xrender::scene* scene);
    ~window() override;

    void performPaint(paint_type mask, QRegion region, WindowPaintData data) override;
    QRegion transformedShape() const;
    void setTransformedShape(QRegion const& shape);

    static void cleanup();

protected:
    render::window_pixmap* createWindowPixmap() override;

private:
    QRect mapToScreen(paint_type mask, const WindowPaintData& data, const QRect& rect) const;
    QPoint mapToScreen(paint_type mask, const WindowPaintData& data, const QPoint& point) const;

    QRect bufferToWindowRect(const QRect& rect) const;
    QRegion bufferToWindowRegion(const QRegion& region) const;

    void prepareTempPixmap();
    void setPictureFilter(xcb_render_picture_t pic, image_filter_type filter);

    xrender::scene* m_scene;
    xcb_render_pictformat_t format;
    QRegion transformed_shape;

    static QRect temp_visibleRect;

    static XRenderPicture* s_tempPicture;
    static XRenderPicture* s_fadeAlphaPicture;
};

class window_pixmap : public render::window_pixmap
{
public:
    window_pixmap(render::window* window, xcb_render_pictformat_t format);
    ~window_pixmap() override;

    xcb_render_picture_t picture() const;
    void create() override;

private:
    xcb_render_picture_t m_picture;
    xcb_render_pictformat_t m_format;
};

}
