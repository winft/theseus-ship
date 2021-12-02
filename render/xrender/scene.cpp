/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Fredrik Höglund <fredrik@kde.org>
Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "scene.h"

#include "backend.h"
#include "effect_frame.h"
#include "shadow.h"
#include "window.h"

#include "decorations/decoratedclient.h"
#include "render/effects.h"
#include "render/shadow.h"
#include "toplevel.h"

#include "win/geo.h"
#include "win/scene.h"
#include "win/x11/window.h"

#include <cassert>
#include <kwineffectquickview.h>

namespace KWin::render::xrender
{

ScreenPaintData scene::screen_paint;

scene::scene(xrender::backend* backend)
    : render::scene()
    , m_backend(backend)
{
}

scene::~scene()
{
    window::cleanup();
    effect_frame::cleanup();
}

bool scene::initFailed() const
{
    return false;
}

// the entry point for painting
int64_t scene::paint(QRegion damage,
                     std::deque<Toplevel*> const& toplevels,
                     std::chrono::milliseconds presentTime)
{
    QElapsedTimer renderTimer;
    renderTimer.start();

    createStackingOrder(toplevels);

    auto mask = paint_type::none;
    QRegion updateRegion, validRegion;
    paintScreen(mask, damage, QRegion(), &updateRegion, &validRegion, presentTime);

    m_backend->showOverlay();

    m_backend->present(mask, updateRegion);
    // do cleanup
    clearStackingOrder();

    return renderTimer.nsecsElapsed();
}

void scene::paintGenericScreen(paint_type mask, ScreenPaintData data)
{
    screen_paint = data; // save, transformations will be done when painting windows
    render::scene::paintGenericScreen(mask, data);
}

void scene::paintDesktop(int desktop, paint_type mask, const QRegion& region, ScreenPaintData& data)
{
    PaintClipper::push(region);
    render::scene::paintDesktop(desktop, mask, region, data);
    PaintClipper::pop(region);
}

// fill the screen background
void scene::paintBackground(QRegion region)
{
    xcb_render_color_t col = {0, 0, 0, 0xffff}; // black
    const QVector<xcb_rectangle_t>& rects = Xcb::regionToRects(region);
    xcb_render_fill_rectangles(connection(),
                               XCB_RENDER_PICT_OP_SRC,
                               xrenderBufferPicture(),
                               col,
                               rects.count(),
                               rects.data());
}

render::window* scene::createWindow(Toplevel* toplevel)
{
    return new window(toplevel, this);
}

render::effect_frame* scene::createEffectFrame(effect_frame_impl* frame)
{
    return new effect_frame(frame);
}

render::shadow* scene::createShadow(Toplevel* toplevel)
{
    return new shadow(toplevel);
}

Decoration::Renderer* scene::createDecorationRenderer(Decoration::DecoratedClientImpl* client)
{
    return new deco_renderer(client);
}

deco_renderer::deco_renderer(Decoration::DecoratedClientImpl* client)
    : Renderer(client)
    , m_gc(XCB_NONE)
{
    connect(this,
            &Renderer::renderScheduled,
            client->client(),
            static_cast<void (Toplevel::*)(QRegion const&)>(&Toplevel::addRepaint));
    for (int i = 0; i < int(DecorationPart::Count); ++i) {
        m_pixmaps[i] = XCB_PIXMAP_NONE;
        m_pictures[i] = nullptr;
    }
}

deco_renderer::~deco_renderer()
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

void deco_renderer::render()
{
    QRegion scheduled = getScheduled();
    if (scheduled.isEmpty()) {
        return;
    }
    if (areImageSizesDirty()) {
        resizePixmaps();
        resetImageSizesDirty();
        scheduled = QRect(QPoint(), client()->client()->size());
    }

    const QRect top(QPoint(0, 0), m_sizes[int(DecorationPart::Top)]);
    const QRect left(QPoint(0, top.height()), m_sizes[int(DecorationPart::Left)]);
    const QRect right(
        QPoint(top.width() - m_sizes[int(DecorationPart::Right)].width(), top.height()),
        m_sizes[int(DecorationPart::Right)]);
    const QRect bottom(QPoint(0, left.y() + left.height()), m_sizes[int(DecorationPart::Bottom)]);

    xcb_connection_t* c = connection();
    if (m_gc == 0) {
        m_gc = xcb_generate_id(connection());
        xcb_create_gc(c, m_gc, m_pixmaps[int(DecorationPart::Top)], 0, nullptr);
    }
    auto renderPart = [this, c](const QRect& geo, const QPoint& offset, int index) {
        if (!geo.isValid()) {
            return;
        }
        QImage image = renderToImage(geo);
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
    const QRect geometry = scheduled.boundingRect();
    renderPart(left.intersected(geometry), left.topLeft(), int(DecorationPart::Left));
    renderPart(top.intersected(geometry), top.topLeft(), int(DecorationPart::Top));
    renderPart(right.intersected(geometry), right.topLeft(), int(DecorationPart::Right));
    renderPart(bottom.intersected(geometry), bottom.topLeft(), int(DecorationPart::Bottom));
    xcb_flush(c);
}

void deco_renderer::resizePixmaps()
{
    QRect left, top, right, bottom;
    client()->client()->layoutDecorationRects(left, top, right, bottom);

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
                xcb_create_pixmap(
                    connection(), 32, m_pixmaps[border], rootWindow(), size.width(), size.height());
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

xcb_render_picture_t deco_renderer::picture(deco_renderer::DecorationPart part) const
{
    Q_ASSERT(part != DecorationPart::Count);
    XRenderPicture* picture = m_pictures[int(part)];
    if (!picture) {
        return XCB_RENDER_PICTURE_NONE;
    }
    return *picture;
}

void deco_renderer::reparent(Toplevel* window)
{
    render();
    Renderer::reparent(window);
}

render::scene* create_scene(render::compositor* compositor)
{
    QScopedPointer<xrender::backend> backend;
    backend.reset(new xrender::backend(compositor));
    if (backend->isFailed()) {
        return nullptr;
    }
    return new scene(backend.take());
}

void scene::paintCursor()
{
}

void scene::paintEffectQuickView(KWin::EffectQuickView* w)
{
    const QImage buffer = w->bufferAsImage();
    if (buffer.isNull()) {
        return;
    }
    XRenderPicture picture(buffer);
    xcb_render_composite(connection(),
                         XCB_RENDER_PICT_OP_OVER,
                         picture,
                         XCB_RENDER_PICTURE_NONE,
                         effects->xrenderBufferPicture(),
                         0,
                         0,
                         0,
                         0,
                         w->geometry().x(),
                         w->geometry().y(),
                         w->geometry().width(),
                         w->geometry().height());
}

}
