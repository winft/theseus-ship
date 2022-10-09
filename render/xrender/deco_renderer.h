/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"
#include "win/deco/renderer.h"

#include <kwinxrender/utils.h>

namespace KWin::render::xrender
{

enum class DecorationPart : int {
    Left,
    Top,
    Right,
    Bottom,
    Count,
};

class deco_render_data : public win::deco::render_data
{
public:
    ~deco_render_data() override
    {
        for (int i = 0; i < int(DecorationPart::Count); ++i) {
            if (pixmaps[i] != XCB_PIXMAP_NONE) {
                xcb_free_pixmap(connection(), pixmaps[i]);
            }
            delete pictures[i];
        }
        if (gc != 0) {
            xcb_free_gc(connection(), gc);
        }
    }

    xcb_render_picture_t picture(DecorationPart part) const
    {
        assert(part != DecorationPart::Count);
        auto picture = pictures[int(part)];
        if (!picture) {
            return XCB_RENDER_PICTURE_NONE;
        }
        return *picture;
    }

    XRenderPicture* pictures[int(DecorationPart::Count)];
    xcb_pixmap_t pixmaps[int(DecorationPart::Count)];
    xcb_gcontext_t gc{XCB_NONE};
};

class deco_renderer : public win::deco::render_injector
{
public:
    explicit deco_renderer(win::deco::render_window window)
        : win::deco::render_injector(std::move(window))
    {
        this->data = std::make_unique<deco_render_data>();

        auto& data = get_data();
        for (int i = 0; i < int(DecorationPart::Count); ++i) {
            data.pixmaps[i] = XCB_PIXMAP_NONE;
            data.pictures[i] = nullptr;
        }
    }

    void render() override
    {
        auto scheduled = this->getScheduled();
        if (scheduled.isEmpty()) {
            return;
        }
        if (this->image_size_dirty) {
            resizePixmaps();
            this->image_size_dirty = false;
            scheduled = QRect({}, this->window.geo().size());
        }

        const QRect top(QPoint(0, 0), m_sizes[int(DecorationPart::Top)]);
        const QRect left(QPoint(0, top.height()), m_sizes[int(DecorationPart::Left)]);
        const QRect right(
            QPoint(top.width() - m_sizes[int(DecorationPart::Right)].width(), top.height()),
            m_sizes[int(DecorationPart::Right)]);
        const QRect bottom(QPoint(0, left.y() + left.height()),
                           m_sizes[int(DecorationPart::Bottom)]);

        xcb_connection_t* c = connection();
        auto& data = get_data();

        if (data.gc == 0) {
            data.gc = xcb_generate_id(connection());
            xcb_create_gc(c, data.gc, data.pixmaps[int(DecorationPart::Top)], 0, nullptr);
        }
        auto renderPart = [this, c](const QRect& geo, const QPoint& offset, int index) {
            if (!geo.isValid()) {
                return;
            }

            auto& data = get_data();
            auto image = this->renderToImage(geo);
            Q_ASSERT(image.devicePixelRatio() == 1);
            xcb_put_image(c,
                          XCB_IMAGE_FORMAT_Z_PIXMAP,
                          data.pixmaps[index],
                          data.gc,
                          image.width(),
                          image.height(),
                          geo.x() - offset.x(),
                          geo.y() - offset.y(),
                          0,
                          32,
                          image.sizeInBytes(),
                          image.constBits());
        };
        auto const geometry = scheduled.boundingRect();
        renderPart(left.intersected(geometry), left.topLeft(), int(DecorationPart::Left));
        renderPart(top.intersected(geometry), top.topLeft(), int(DecorationPart::Top));
        renderPart(right.intersected(geometry), right.topLeft(), int(DecorationPart::Right));
        renderPart(bottom.intersected(geometry), bottom.topLeft(), int(DecorationPart::Bottom));
        xcb_flush(c);
    }

private:
    deco_render_data& get_data()
    {
        return static_cast<deco_render_data&>(*this->data);
    }

    void resizePixmaps()
    {
        QRect left, top, right, bottom;
        this->window.layout_rects(left, top, right, bottom);
        xcb_connection_t* c = connection();

        auto checkAndCreate = [this, c](int border, const QRect& rect) {
            auto& data = get_data();
            const QSize size = rect.size();

            if (m_sizes[border] != size) {
                m_sizes[border] = size;
                if (data.pixmaps[border] != XCB_PIXMAP_NONE) {
                    xcb_free_pixmap(c, data.pixmaps[border]);
                }
                delete data.pictures[border];
                if (!size.isEmpty()) {
                    data.pixmaps[border] = xcb_generate_id(connection());
                    xcb_create_pixmap(connection(),
                                      32,
                                      data.pixmaps[border],
                                      rootWindow(),
                                      size.width(),
                                      size.height());
                    data.pictures[border] = new XRenderPicture(data.pixmaps[border], 32);
                } else {
                    data.pixmaps[border] = XCB_PIXMAP_NONE;
                    data.pictures[border] = nullptr;
                }
            }
            if (!data.pictures[border]) {
                return;
            }
            // fill transparent
            xcb_rectangle_t r = {0, 0, uint16_t(size.width()), uint16_t(size.height())};
            xcb_render_fill_rectangles(connection(),
                                       XCB_RENDER_PICT_OP_SRC,
                                       *data.pictures[border],
                                       preMultiply(Qt::transparent),
                                       1,
                                       &r);
        };

        checkAndCreate(int(DecorationPart::Left), left);
        checkAndCreate(int(DecorationPart::Top), top);
        checkAndCreate(int(DecorationPart::Right), right);
        checkAndCreate(int(DecorationPart::Bottom), bottom);
    }

    QSize m_sizes[int(DecorationPart::Count)];
};

}
