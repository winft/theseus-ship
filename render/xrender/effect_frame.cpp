/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effect_frame.h"

#include "render/compositor.h"
#include "render/effects.h"

#include <QPainter>
#include <QtMath>

namespace KWin::render::xrender
{

#define DOUBLE_TO_FIXED(d) ((xcb_render_fixed_t)((d)*65536))

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

    auto& effects = m_effectFrame->scene.compositor.effects;
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

#undef DOUBLE_TO_FIXED

}
