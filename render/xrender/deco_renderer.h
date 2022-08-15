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

template<typename Client>
class deco_renderer : public win::deco::renderer<Client>
{
public:
    explicit deco_renderer(Client* client)
        : win::deco::renderer<Client>(client)
        , m_gc(XCB_NONE)
    {
        QObject::connect(this->qobject.get(),
                         &win::deco::renderer_qobject::renderScheduled,
                         client->client()->qobject.get(),
                         [win = client->client()](auto const& region) { win->addRepaint(region); });
        for (int i = 0; i < int(DecorationPart::Count); ++i) {
            m_pixmaps[i] = XCB_PIXMAP_NONE;
            m_pictures[i] = nullptr;
        }
    }

    ~deco_renderer() override
    {
        for (int i = 0; i < int(DecorationPart::Count); ++i) {
            if (m_pixmaps[i] != XCB_PIXMAP_NONE) {
                xcb_free_pixmap(connection(), m_pixmaps[i]);
            }
            delete m_pictures[i];
        }
        if (m_gc != 0) {
            xcb_free_gc(connection(), m_gc);
        }
    }

    void render() override
    {
        auto scheduled = this->getScheduled();
        if (scheduled.isEmpty()) {
            return;
        }
        if (this->areImageSizesDirty()) {
            resizePixmaps();
            this->resetImageSizesDirty();
            scheduled = QRect(QPoint(), this->client()->client()->size());
        }

        const QRect top(QPoint(0, 0), m_sizes[int(DecorationPart::Top)]);
        const QRect left(QPoint(0, top.height()), m_sizes[int(DecorationPart::Left)]);
        const QRect right(
            QPoint(top.width() - m_sizes[int(DecorationPart::Right)].width(), top.height()),
            m_sizes[int(DecorationPart::Right)]);
        const QRect bottom(QPoint(0, left.y() + left.height()),
                           m_sizes[int(DecorationPart::Bottom)]);

        xcb_connection_t* c = connection();
        if (m_gc == 0) {
            m_gc = xcb_generate_id(connection());
            xcb_create_gc(c, m_gc, m_pixmaps[int(DecorationPart::Top)], 0, nullptr);
        }
        auto renderPart = [this, c](const QRect& geo, const QPoint& offset, int index) {
            if (!geo.isValid()) {
                return;
            }
            auto image = this->renderToImage(geo);
            Q_ASSERT(image.devicePixelRatio() == 1);
            xcb_put_image(c,
                          XCB_IMAGE_FORMAT_Z_PIXMAP,
                          m_pixmaps[index],
                          m_gc,
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

    void reparent() override
    {
        render();
        win::deco::renderer<Client>::reparent();
    }

    xcb_render_picture_t picture(DecorationPart part) const
    {
        Q_ASSERT(part != DecorationPart::Count);
        XRenderPicture* picture = m_pictures[int(part)];
        if (!picture) {
            return XCB_RENDER_PICTURE_NONE;
        }
        return *picture;
    }

private:
    void resizePixmaps()
    {
        QRect left, top, right, bottom;
        this->client()->client()->layoutDecorationRects(left, top, right, bottom);

        xcb_connection_t* c = connection();
        auto checkAndCreate = [this, c](int border, const QRect& rect) {
            const QSize size = rect.size();
            if (m_sizes[border] != size) {
                m_sizes[border] = size;
                if (m_pixmaps[border] != XCB_PIXMAP_NONE) {
                    xcb_free_pixmap(c, m_pixmaps[border]);
                }
                delete m_pictures[border];
                if (!size.isEmpty()) {
                    m_pixmaps[border] = xcb_generate_id(connection());
                    xcb_create_pixmap(connection(),
                                      32,
                                      m_pixmaps[border],
                                      rootWindow(),
                                      size.width(),
                                      size.height());
                    m_pictures[border] = new XRenderPicture(m_pixmaps[border], 32);
                } else {
                    m_pixmaps[border] = XCB_PIXMAP_NONE;
                    m_pictures[border] = nullptr;
                }
            }
            if (!m_pictures[border]) {
                return;
            }
            // fill transparent
            xcb_rectangle_t r = {0, 0, uint16_t(size.width()), uint16_t(size.height())};
            xcb_render_fill_rectangles(connection(),
                                       XCB_RENDER_PICT_OP_SRC,
                                       *m_pictures[border],
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
    xcb_pixmap_t m_pixmaps[int(DecorationPart::Count)];
    xcb_gcontext_t m_gc;
    XRenderPicture* m_pictures[int(DecorationPart::Count)];
};

}
