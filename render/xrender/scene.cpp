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

#include "window.h"

#include "decorations/decoratedclient.h"
#include "render/effects.h"
#include "render/x11/compositor.h"
#include "render/x11/overlay_window.h"
#include "screens.h"
#include "toplevel.h"
#include "utils.h"
#include "xcbutils.h"

#include "win/geo.h"
#include "win/scene.h"
#include "win/x11/window.h"

#include <kwineffectquickview.h>
#include <kwinxrenderutils.h>

#include <QPainter>
#include <QtMath>
#include <cassert>
#include <xcb/xfixes.h>

namespace KWin::render::xrender
{

ScreenPaintData scene::screen_paint;

#define DOUBLE_TO_FIXED(d) ((xcb_render_fixed_t)((d)*65536))
#define FIXED_TO_DOUBLE(f) ((double)((f) / 65536.0))

//****************************************
// backend
//****************************************
backend::backend(render::compositor* compositor)
    : overlay_window{std::make_unique<render::x11::overlay_window>()}
{
    if (!Xcb::Extensions::self()->isRenderAvailable()) {
        setFailed("No XRender extension available");
        return;
    }
    if (!Xcb::Extensions::self()->isFixesRegionAvailable()) {
        setFailed("No XFixes v3+ extension available");
        return;
    }

    auto x11_compositor = dynamic_cast<render::x11::compositor*>(compositor);
    assert(x11_compositor);
    x11_compositor->overlay_window = overlay_window.get();

    init(true);
}

backend::~backend()
{
    if (m_front) {
        xcb_render_free_picture(connection(), m_front);
    }

    overlay_window->destroy();

    if (m_buffer) {
        xcb_render_free_picture(connection(), m_buffer);
    }
    overlay_window.reset();
}

void backend::setBuffer(xcb_render_picture_t buffer)
{
    if (m_buffer != XCB_RENDER_PICTURE_NONE) {
        xcb_render_free_picture(connection(), m_buffer);
    }
    m_buffer = buffer;
}

void backend::setFailed(const QString& reason)
{
    qCCritical(KWIN_CORE) << "Creating the XRender backend failed: " << reason;
    m_failed = true;
}

void backend::showOverlay()
{
    // Show the window only after the first pass, since that pass may take long.
    if (overlay_window->window()) {
        overlay_window->show();
    }
}

void backend::init(bool createOverlay)
{
    if (m_front != XCB_RENDER_PICTURE_NONE)
        xcb_render_free_picture(connection(), m_front);
    bool haveOverlay
        = createOverlay ? overlay_window->create() : (overlay_window->window() != XCB_WINDOW_NONE);
    if (haveOverlay) {
        overlay_window->setup(XCB_WINDOW_NONE);
        ScopedCPointer<xcb_get_window_attributes_reply_t> attribs(xcb_get_window_attributes_reply(
            connection(),
            xcb_get_window_attributes_unchecked(connection(), overlay_window->window()),
            nullptr));
        if (!attribs) {
            setFailed("Failed getting window attributes for overlay window");
            return;
        }
        m_format = XRenderUtils::findPictFormat(attribs->visual);
        if (m_format == 0) {
            setFailed("Failed to find XRender format for overlay window");
            return;
        }
        m_front = xcb_generate_id(connection());
        xcb_render_create_picture(
            connection(), m_front, overlay_window->window(), m_format, 0, nullptr);
    } else {
        // create XRender picture for the root window
        m_format = XRenderUtils::findPictFormat(defaultScreen()->root_visual);
        if (m_format == 0) {
            setFailed("Failed to find XRender format for root window");
            return; // error
        }
        m_front = xcb_generate_id(connection());
        const uint32_t values[] = {XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS};
        xcb_render_create_picture(
            connection(), m_front, rootWindow(), m_format, XCB_RENDER_CP_SUBWINDOW_MODE, values);
    }
    createBuffer();
}

void backend::createBuffer()
{
    xcb_pixmap_t pixmap = xcb_generate_id(connection());
    const auto displaySize = screens()->displaySize();
    xcb_create_pixmap(connection(),
                      Xcb::defaultDepth(),
                      pixmap,
                      rootWindow(),
                      displaySize.width(),
                      displaySize.height());
    xcb_render_picture_t b = xcb_generate_id(connection());
    xcb_render_create_picture(connection(), b, pixmap, m_format, 0, nullptr);
    xcb_free_pixmap(connection(), pixmap); // The picture owns the pixmap now
    setBuffer(b);
}

void backend::present(paint_type mask, const QRegion& damage)
{
    const auto displaySize = screens()->displaySize();
    if (flags(mask & paint_type::screen_region)) {
        // Use the damage region as the clip region for the root window
        XFixesRegion frontRegion(damage);
        xcb_xfixes_set_picture_clip_region(connection(), m_front, frontRegion, 0, 0);
        // copy composed buffer to the root window
        xcb_xfixes_set_picture_clip_region(connection(), buffer(), XCB_XFIXES_REGION_NONE, 0, 0);
        xcb_render_composite(connection(),
                             XCB_RENDER_PICT_OP_SRC,
                             buffer(),
                             XCB_RENDER_PICTURE_NONE,
                             m_front,
                             0,
                             0,
                             0,
                             0,
                             0,
                             0,
                             displaySize.width(),
                             displaySize.height());
        xcb_xfixes_set_picture_clip_region(connection(), m_front, XCB_XFIXES_REGION_NONE, 0, 0);
        xcb_flush(connection());
    } else {
        // copy composed buffer to the root window
        xcb_render_composite(connection(),
                             XCB_RENDER_PICT_OP_SRC,
                             buffer(),
                             XCB_RENDER_PICTURE_NONE,
                             m_front,
                             0,
                             0,
                             0,
                             0,
                             0,
                             0,
                             displaySize.width(),
                             displaySize.height());
        xcb_flush(connection());
    }
}

void backend::screenGeometryChanged(const QSize& size)
{
    overlay_window->resize(size);
    init(false);
}

//****************************************
// scene
//****************************************

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

//****************************************
// effect_frame
//****************************************

XRenderPicture* effect_frame::s_effectFrameCircle = nullptr;

effect_frame::effect_frame(effect_frame_impl* frame)
    : render::effect_frame(frame)
{
    m_picture = nullptr;
    m_textPicture = nullptr;
    m_iconPicture = nullptr;
    m_selectionPicture = nullptr;
}

effect_frame::~effect_frame()
{
    delete m_picture;
    delete m_textPicture;
    delete m_iconPicture;
    delete m_selectionPicture;
}

void effect_frame::cleanup()
{
    delete s_effectFrameCircle;
    s_effectFrameCircle = nullptr;
}

void effect_frame::free()
{
    delete m_picture;
    m_picture = nullptr;
    delete m_textPicture;
    m_textPicture = nullptr;
    delete m_iconPicture;
    m_iconPicture = nullptr;
    delete m_selectionPicture;
    m_selectionPicture = nullptr;
}

void effect_frame::freeIconFrame()
{
    delete m_iconPicture;
    m_iconPicture = nullptr;
}

void effect_frame::freeTextFrame()
{
    delete m_textPicture;
    m_textPicture = nullptr;
}

void effect_frame::freeSelection()
{
    delete m_selectionPicture;
    m_selectionPicture = nullptr;
}

void effect_frame::crossFadeIcon()
{
    // TODO: implement me
}

void effect_frame::crossFadeText()
{
    // TODO: implement me
}

void effect_frame::render(QRegion region, double opacity, double frameOpacity)
{
    Q_UNUSED(region)
    if (m_effectFrame->geometry().isEmpty()) {
        return; // Nothing to display
    }

    // Render the actual frame
    if (m_effectFrame->style() == EffectFrameUnstyled) {
        renderUnstyled(
            effects->xrenderBufferPicture(), m_effectFrame->geometry(), opacity * frameOpacity);
    } else if (m_effectFrame->style() == EffectFrameStyled) {
        if (!m_picture) { // Lazy creation
            updatePicture();
        }
        if (m_picture) {
            qreal left, top, right, bottom;
            m_effectFrame->frame().getMargins(
                left, top, right, bottom); // m_geometry is the inner geometry
            QRect geom = m_effectFrame->geometry().adjusted(-left, -top, right, bottom);
            xcb_render_composite(connection(),
                                 XCB_RENDER_PICT_OP_OVER,
                                 *m_picture,
                                 XCB_RENDER_PICTURE_NONE,
                                 effects->xrenderBufferPicture(),
                                 0,
                                 0,
                                 0,
                                 0,
                                 geom.x(),
                                 geom.y(),
                                 geom.width(),
                                 geom.height());
        }
    }
    if (!m_effectFrame->selection().isNull()) {
        if (!m_selectionPicture) { // Lazy creation
            const QPixmap pix = m_effectFrame->selectionFrame().framePixmap();
            if (!pix.isNull()) // don't try if there's no content
                m_selectionPicture
                    = new XRenderPicture(m_effectFrame->selectionFrame().framePixmap().toImage());
        }
        if (m_selectionPicture) {
            const QRect geom = m_effectFrame->selection();
            xcb_render_composite(connection(),
                                 XCB_RENDER_PICT_OP_OVER,
                                 *m_selectionPicture,
                                 XCB_RENDER_PICTURE_NONE,
                                 effects->xrenderBufferPicture(),
                                 0,
                                 0,
                                 0,
                                 0,
                                 geom.x(),
                                 geom.y(),
                                 geom.width(),
                                 geom.height());
        }
    }

    XRenderPicture fill = xRenderBlendPicture(opacity);

    // Render icon
    if (!m_effectFrame->icon().isNull() && !m_effectFrame->iconSize().isEmpty()) {
        QPoint topLeft(m_effectFrame->geometry().x(),
                       m_effectFrame->geometry().center().y()
                           - m_effectFrame->iconSize().height() / 2);

        if (!m_iconPicture) // lazy creation
            m_iconPicture = new XRenderPicture(
                m_effectFrame->icon().pixmap(m_effectFrame->iconSize()).toImage());
        QRect geom = QRect(topLeft, m_effectFrame->iconSize());
        xcb_render_composite(connection(),
                             XCB_RENDER_PICT_OP_OVER,
                             *m_iconPicture,
                             fill,
                             effects->xrenderBufferPicture(),
                             0,
                             0,
                             0,
                             0,
                             geom.x(),
                             geom.y(),
                             geom.width(),
                             geom.height());
    }

    // Render text
    if (!m_effectFrame->text().isEmpty()) {
        if (!m_textPicture) { // Lazy creation
            updateTextPicture();
        }

        if (m_textPicture) {
            xcb_render_composite(connection(),
                                 XCB_RENDER_PICT_OP_OVER,
                                 *m_textPicture,
                                 fill,
                                 effects->xrenderBufferPicture(),
                                 0,
                                 0,
                                 0,
                                 0,
                                 m_effectFrame->geometry().x(),
                                 m_effectFrame->geometry().y(),
                                 m_effectFrame->geometry().width(),
                                 m_effectFrame->geometry().height());
        }
    }
}

void effect_frame::renderUnstyled(xcb_render_picture_t pict, const QRect& rect, qreal opacity)
{
    const int roundness = 5;
    const QRect area = rect.adjusted(-roundness, -roundness, roundness, roundness);
    xcb_rectangle_t rects[3];
    // center
    rects[0].x = area.left();
    rects[0].y = area.top() + roundness;
    rects[0].width = area.width();
    rects[0].height = area.height() - roundness * 2;
    // top
    rects[1].x = area.left() + roundness;
    rects[1].y = area.top();
    rects[1].width = area.width() - roundness * 2;
    rects[1].height = roundness;
    // bottom
    rects[2].x = area.left() + roundness;
    rects[2].y = area.top() + area.height() - roundness;
    rects[2].width = area.width() - roundness * 2;
    rects[2].height = roundness;
    xcb_render_color_t color = {0, 0, 0, uint16_t(opacity * 0xffff)};
    xcb_render_fill_rectangles(connection(), XCB_RENDER_PICT_OP_OVER, pict, color, 3, rects);

    if (!s_effectFrameCircle) {
        // create the circle
        const int diameter = roundness * 2;
        xcb_pixmap_t pix = xcb_generate_id(connection());
        xcb_create_pixmap(connection(), 32, pix, rootWindow(), diameter, diameter);
        s_effectFrameCircle = new XRenderPicture(pix, 32);
        xcb_free_pixmap(connection(), pix);

        // clear it with transparent
        xcb_rectangle_t xrect = {0, 0, diameter, diameter};
        xcb_render_color_t tranparent = {0, 0, 0, 0};
        xcb_render_fill_rectangles(
            connection(), XCB_RENDER_PICT_OP_SRC, *s_effectFrameCircle, tranparent, 1, &xrect);

        static const int num_segments = 80;
        static const qreal theta = 2 * M_PI / qreal(num_segments);
        static const qreal c = qCos(theta); // precalculate the sine and cosine
        static const qreal s = qSin(theta);
        qreal t;

        qreal x = roundness; // we start at angle = 0
        qreal y = 0;

        QVector<xcb_render_pointfix_t> points;
        xcb_render_pointfix_t point;
        point.x = DOUBLE_TO_FIXED(roundness);
        point.y = DOUBLE_TO_FIXED(roundness);
        points << point;
        for (int ii = 0; ii <= num_segments; ++ii) {
            point.x = DOUBLE_TO_FIXED(x + roundness);
            point.y = DOUBLE_TO_FIXED(y + roundness);
            points << point;
            // apply the rotation matrix
            t = x;
            x = c * x - s * y;
            y = s * t + c * y;
        }
        XRenderPicture fill = xRenderFill(Qt::black);
        xcb_render_tri_fan(connection(),
                           XCB_RENDER_PICT_OP_OVER,
                           fill,
                           *s_effectFrameCircle,
                           0,
                           0,
                           0,
                           points.count(),
                           points.constData());
    }
    // TODO: merge alpha mask with window::alphaMask
    // alpha mask
    xcb_pixmap_t pix = xcb_generate_id(connection());
    xcb_create_pixmap(connection(), 8, pix, rootWindow(), 1, 1);
    XRenderPicture alphaMask(pix, 8);
    xcb_free_pixmap(connection(), pix);
    const uint32_t values[] = {true};
    xcb_render_change_picture(connection(), alphaMask, XCB_RENDER_CP_REPEAT, values);
    color.alpha = int(opacity * 0xffff);
    xcb_rectangle_t xrect = {0, 0, 1, 1};
    xcb_render_fill_rectangles(connection(), XCB_RENDER_PICT_OP_SRC, alphaMask, color, 1, &xrect);

    // TODO: replace by lambda
#define RENDER_CIRCLE(srcX, srcY, destX, destY)                                                    \
    xcb_render_composite(connection(),                                                             \
                         XCB_RENDER_PICT_OP_OVER,                                                  \
                         *s_effectFrameCircle,                                                     \
                         alphaMask,                                                                \
                         pict,                                                                     \
                         srcX,                                                                     \
                         srcY,                                                                     \
                         0,                                                                        \
                         0,                                                                        \
                         destX,                                                                    \
                         destY,                                                                    \
                         roundness,                                                                \
                         roundness)

    RENDER_CIRCLE(0, 0, area.left(), area.top());
    RENDER_CIRCLE(0, roundness, area.left(), area.top() + area.height() - roundness);
    RENDER_CIRCLE(roundness, 0, area.left() + area.width() - roundness, area.top());
    RENDER_CIRCLE(roundness,
                  roundness,
                  area.left() + area.width() - roundness,
                  area.top() + area.height() - roundness);
#undef RENDER_CIRCLE
}

void effect_frame::updatePicture()
{
    delete m_picture;
    m_picture = nullptr;
    if (m_effectFrame->style() == EffectFrameStyled) {
        const QPixmap pix = m_effectFrame->frame().framePixmap();
        if (!pix.isNull())
            m_picture = new XRenderPicture(pix.toImage());
    }
}

void effect_frame::updateTextPicture()
{
    // Mostly copied from SceneOpenGL::EffectFrame::updateTextTexture() above
    delete m_textPicture;
    m_textPicture = nullptr;

    if (m_effectFrame->text().isEmpty()) {
        return;
    }

    // Determine position on texture to paint text
    QRect rect(QPoint(0, 0), m_effectFrame->geometry().size());
    if (!m_effectFrame->icon().isNull() && !m_effectFrame->iconSize().isEmpty()) {
        rect.setLeft(m_effectFrame->iconSize().width());
    }

    // If static size elide text as required
    QString text = m_effectFrame->text();
    if (m_effectFrame->isStatic()) {
        QFontMetrics metrics(m_effectFrame->text());
        text = metrics.elidedText(text, Qt::ElideRight, rect.width());
    }

    QPixmap pixmap(m_effectFrame->geometry().size());
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setFont(m_effectFrame->font());
    if (m_effectFrame->style() == EffectFrameStyled) {
        p.setPen(m_effectFrame->styledTextColor());
    } else {
        // TODO: What about no frame? Custom color setting required
        p.setPen(Qt::white);
    }
    p.drawText(rect, m_effectFrame->alignment(), text);
    p.end();
    m_textPicture = new XRenderPicture(pixmap.toImage());
}

shadow::shadow(Toplevel* toplevel)
    : render::shadow(toplevel)
{
    for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
        m_pictures[i] = nullptr;
    }
}

shadow::~shadow()
{
    for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
        delete m_pictures[i];
    }
}

void shadow::layoutShadowRects(QRect& top,
                               QRect& topRight,
                               QRect& right,
                               QRect& bottomRight,
                               QRect& bottom,
                               QRect& bottomLeft,
                               QRect& left,
                               QRect& topLeft)
{
    WindowQuadList quads = shadowQuads();

    if (quads.count() == 0) {
        return;
    }

    WindowQuad topQuad = quads.select(WindowQuadShadowTop)[0];
    WindowQuad topRightQuad = quads.select(WindowQuadShadowTopRight)[0];
    WindowQuad topLeftQuad = quads.select(WindowQuadShadowTopLeft)[0];
    WindowQuad leftQuad = quads.select(WindowQuadShadowLeft)[0];
    WindowQuad rightQuad = quads.select(WindowQuadShadowRight)[0];
    WindowQuad bottomQuad = quads.select(WindowQuadShadowBottom)[0];
    WindowQuad bottomRightQuad = quads.select(WindowQuadShadowBottomRight)[0];
    WindowQuad bottomLeftQuad = quads.select(WindowQuadShadowBottomLeft)[0];

    top = QRect(topQuad.left(),
                topQuad.top(),
                (topQuad.right() - topQuad.left()),
                (topQuad.bottom() - topQuad.top()));
    topLeft = QRect(topLeftQuad.left(),
                    topLeftQuad.top(),
                    (topLeftQuad.right() - topLeftQuad.left()),
                    (topLeftQuad.bottom() - topLeftQuad.top()));
    topRight = QRect(topRightQuad.left(),
                     topRightQuad.top(),
                     (topRightQuad.right() - topRightQuad.left()),
                     (topRightQuad.bottom() - topRightQuad.top()));
    left = QRect(leftQuad.left(),
                 leftQuad.top(),
                 (leftQuad.right() - leftQuad.left()),
                 (leftQuad.bottom() - leftQuad.top()));
    right = QRect(rightQuad.left(),
                  rightQuad.top(),
                  (rightQuad.right() - rightQuad.left()),
                  (rightQuad.bottom() - rightQuad.top()));
    bottom = QRect(bottomQuad.left(),
                   bottomQuad.top(),
                   (bottomQuad.right() - bottomQuad.left()),
                   (bottomQuad.bottom() - bottomQuad.top()));
    bottomLeft = QRect(bottomLeftQuad.left(),
                       bottomLeftQuad.top(),
                       (bottomLeftQuad.right() - bottomLeftQuad.left()),
                       (bottomLeftQuad.bottom() - bottomLeftQuad.top()));
    bottomRight = QRect(bottomRightQuad.left(),
                        bottomRightQuad.top(),
                        (bottomRightQuad.right() - bottomRightQuad.left()),
                        (bottomRightQuad.bottom() - bottomRightQuad.top()));
}

void shadow::buildQuads()
{
    render::shadow::buildQuads();

    if (shadowQuads().count() == 0) {
        return;
    }

    QRect stlr, str, strr, srr, sbrr, sbr, sblr, slr;
    layoutShadowRects(str, strr, srr, sbrr, sbr, sblr, slr, stlr);
}

bool shadow::prepareBackend()
{
    if (hasDecorationShadow()) {
        const QImage shadowImage = decorationShadowImage();
        QPainter p;
        int x = 0;
        int y = 0;
        auto drawElement = [this, &x, &y, &p, &shadowImage](auto element) {
            QPixmap pix(elementSize(element));
            pix.fill(Qt::transparent);
            p.begin(&pix);
            p.drawImage(0, 0, shadowImage, x, y, pix.width(), pix.height());
            p.end();
            setShadowElement(pix, element);
            return pix.size();
        };
        x += drawElement(shadow_element::top_left).width();
        x += drawElement(shadow_element::top).width();
        y += drawElement(shadow_element::top_right).height();
        drawElement(shadow_element::right);
        x = 0;
        y += drawElement(shadow_element::left).height();
        x += drawElement(shadow_element::bottom_left).width();
        x += drawElement(shadow_element::bottom).width();
        drawElement(shadow_element::bottom_right).width();
    }
    const uint32_t values[] = {XCB_RENDER_REPEAT_NORMAL};
    for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
        delete m_pictures[i];
        m_pictures[i] = new XRenderPicture(shadowPixmap(static_cast<shadow_element>(i)).toImage());
        xcb_render_change_picture(connection(), *m_pictures[i], XCB_RENDER_CP_REPEAT, values);
    }
    return true;
}

xcb_render_picture_t shadow::picture(shadow_element element) const
{
    if (!m_pictures[enum_index(element)]) {
        return XCB_RENDER_PICTURE_NONE;
    }
    return *m_pictures[enum_index(element)];
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

#undef DOUBLE_TO_FIXED
#undef FIXED_TO_DOUBLE

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
