/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "base/logging.h"
#include "deco_shadow.h"
#include "effects.h"
#include "shadow.h"

#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "toplevel.h"
#include "win/geo.h"
#include "win/transient.h"

namespace KWin::render
{

uint32_t window_id{0};

window::window(Toplevel* c)
    : toplevel(c)
    , filter(image_filter_type::fast)
    , m_shadow(nullptr)
    , m_currentPixmap()
    , m_previousPixmap()
    , m_referencePixmapCounter(0)
    , cached_quad_list(nullptr)
    , m_id{window_id++}
{
}

window::~window()
{
    delete m_shadow;
}

uint32_t window::id() const
{
    return m_id;
}

void window::referencePreviousPixmap()
{
    if (m_previousPixmap && m_previousPixmap->isDiscarded()) {
        m_referencePixmapCounter++;
    }
}

void window::unreferencePreviousPixmap()
{
    if (!m_previousPixmap || !m_previousPixmap->isDiscarded()) {
        return;
    }
    m_referencePixmapCounter--;
    if (m_referencePixmapCounter == 0) {
        m_previousPixmap.reset();
    }
}

void window::discardPixmap()
{
    if (m_currentPixmap) {
        if (m_currentPixmap->isValid()) {
            m_previousPixmap.reset(m_currentPixmap.release());
            m_previousPixmap->markAsDiscarded();
        } else {
            m_currentPixmap.reset();
        }
    }
}

void window::updatePixmap()
{
    if (!m_currentPixmap) {
        m_currentPixmap.reset(createWindowPixmap());
    }
    if (!m_currentPixmap->isValid()) {
        m_currentPixmap->create();
    }
}

QRegion window::decorationShape() const
{
    if (!win::decoration(toplevel)) {
        return QRegion();
    }
    return QRegion(QRect(QPoint(), toplevel->size())) - win::frame_relative_client_rect(toplevel);
}

QPoint window::bufferOffset() const
{
    return win::render_geometry(toplevel).topLeft() - toplevel->pos();
}

bool window::isVisible() const
{
    if (toplevel->isDeleted())
        return false;
    if (!toplevel->isOnCurrentDesktop())
        return false;
    if (toplevel->control) {
        return toplevel->isShown();
    }
    return true; // Unmanaged is always visible
}

bool window::isOpaque() const
{
    return toplevel->opacity() == 1.0 && !toplevel->hasAlpha();
}

bool window::isPaintingEnabled() const
{
    return disable_painting == window_paint_disable_type::none;
}

void window::resetPaintingEnabled()
{
    disable_painting = window_paint_disable_type::none;
    if (toplevel->isDeleted()) {
        disable_painting |= window_paint_disable_type::by_delete;
    }
    if (static_cast<effects_handler_impl*>(effects)->isDesktopRendering()) {
        if (!toplevel->isOnDesktop(
                static_cast<effects_handler_impl*>(effects)->currentRenderedDesktop())) {
            disable_painting |= window_paint_disable_type::by_desktop;
        }
    } else {
        if (!toplevel->isOnCurrentDesktop())
            disable_painting |= window_paint_disable_type::by_desktop;
    }
    if (toplevel->control) {
        if (toplevel->control->minimized()) {
            disable_painting |= window_paint_disable_type::by_minimize;
        }
        if (toplevel->isHiddenInternal()) {
            disable_painting |= window_paint_disable_type::unspecified;
        }
    }
}

void window::enablePainting(window_paint_disable_type reason)
{
    disable_painting &= ~reason;
}

void window::disablePainting(window_paint_disable_type reason)
{
    disable_painting |= reason;
}

WindowQuadList window::buildQuads(bool force) const
{
    if (cached_quad_list != nullptr && !force)
        return *cached_quad_list;

    auto ret = makeContentsQuads(id());

    if (!win::frame_margins(toplevel).isNull()) {
        qreal decorationScale = 1.0;

        QRect rects[4];

        if (toplevel->control) {
            toplevel->layoutDecorationRects(rects[0], rects[1], rects[2], rects[3]);
            decorationScale = toplevel->central_output ? toplevel->central_output->scale() : 1.;
        }

        auto const decoration_region = decorationShape();
        ret += makeDecorationQuads(rects, decoration_region, decorationScale);
    }

    if (m_shadow && toplevel->wantsShadowToBeRendered()) {
        ret << m_shadow->shadowQuads();
    }

    effects->buildQuads(effect.get(), ret);
    cached_quad_list.reset(new WindowQuadList(ret));
    return ret;
}

WindowQuadList
window::makeDecorationQuads(const QRect* rects, const QRegion& region, qreal textureScale) const
{
    WindowQuadList list;

    const int padding = 1;

    const QPoint topSpritePosition(padding, padding);
    const QPoint bottomSpritePosition(padding,
                                      topSpritePosition.y() + rects[1].height() + 2 * padding);
    const QPoint leftSpritePosition(bottomSpritePosition.y() + rects[3].height() + 2 * padding,
                                    padding);
    const QPoint rightSpritePosition(leftSpritePosition.x() + rects[0].width() + 2 * padding,
                                     padding);

    const QPoint offsets[4] = {
        QPoint(-rects[0].x(), -rects[0].y()) + leftSpritePosition,
        QPoint(-rects[1].x(), -rects[1].y()) + topSpritePosition,
        QPoint(-rects[2].x(), -rects[2].y()) + rightSpritePosition,
        QPoint(-rects[3].x(), -rects[3].y()) + bottomSpritePosition,
    };

    const Qt::Orientation orientations[4] = {
        Qt::Vertical,   // Left
        Qt::Horizontal, // Top
        Qt::Vertical,   // Right
        Qt::Horizontal, // Bottom
    };

    for (int i = 0; i < 4; i++) {
        const QRegion intersectedRegion = (region & rects[i]);
        for (const QRect& r : intersectedRegion) {
            if (!r.isValid())
                continue;

            const bool swap = orientations[i] == Qt::Vertical;

            const int x0 = r.x();
            const int y0 = r.y();
            const int x1 = r.x() + r.width();
            const int y1 = r.y() + r.height();

            const int u0 = (x0 + offsets[i].x()) * textureScale;
            const int v0 = (y0 + offsets[i].y()) * textureScale;
            const int u1 = (x1 + offsets[i].x()) * textureScale;
            const int v1 = (y1 + offsets[i].y()) * textureScale;

            WindowQuad quad(WindowQuadDecoration);
            quad.setUVAxisSwapped(swap);

            if (swap) {
                quad[0] = WindowVertex(x0, y0, v0, u0); // Top-left
                quad[1] = WindowVertex(x1, y0, v0, u1); // Top-right
                quad[2] = WindowVertex(x1, y1, v1, u1); // Bottom-right
                quad[3] = WindowVertex(x0, y1, v1, u0); // Bottom-left
            } else {
                quad[0] = WindowVertex(x0, y0, u0, v0); // Top-left
                quad[1] = WindowVertex(x1, y0, u1, v0); // Top-right
                quad[2] = WindowVertex(x1, y1, u1, v1); // Bottom-right
                quad[3] = WindowVertex(x0, y1, u0, v1); // Bottom-left
            }

            list.append(quad);
        }
    }

    return list;
}

WindowQuadList window::makeContentsQuads(int id, QPoint const& offset) const
{
    auto const contentsRegion = win::content_render_region(toplevel);
    if (contentsRegion.isEmpty()) {
        return WindowQuadList();
    }

    auto const geometryOffset = offset + bufferOffset();
    const qreal textureScale = toplevel->bufferScale();

    WindowQuadList quads;
    quads.reserve(contentsRegion.rectCount());

    auto createQuad = [id, geometryOffset](QRectF const& rect, QRectF const& sourceRect) {
        WindowQuad quad(WindowQuadContents, id);

        const qreal x0 = rect.left() + geometryOffset.x();
        const qreal y0 = rect.top() + geometryOffset.y();
        const qreal x1 = rect.right() + geometryOffset.x();
        const qreal y1 = rect.bottom() + geometryOffset.y();

        const qreal u0 = sourceRect.left();
        const qreal v0 = sourceRect.top();
        const qreal u1 = sourceRect.right();
        const qreal v1 = sourceRect.bottom();

        quad[0] = WindowVertex(QPointF(x0, y0), QPointF(u0, v0));
        quad[1] = WindowVertex(QPointF(x1, y0), QPointF(u1, v0));
        quad[2] = WindowVertex(QPointF(x1, y1), QPointF(u1, v1));
        quad[3] = WindowVertex(QPointF(x0, y1), QPointF(u0, v1));
        return quad;
    };

    // Check for viewport being set. We only allow specifying the viewport at the moment for
    // non-shape windows.
    if (contentsRegion.rectCount() < 2) {
        QRectF const contentsRect = *contentsRegion.begin();
        QRectF sourceRect(contentsRect.topLeft() * textureScale,
                          contentsRect.bottomRight() * textureScale);
        if (get_wayland_viewport) {
            if (auto vp = get_wayland_viewport(toplevel, contentsRect); vp.isValid()) {
                sourceRect = vp;
            }
        }
        quads << createQuad(contentsRect, sourceRect);
    } else {
        for (auto const& contentsRect : contentsRegion) {
            auto const sourceRect = QRectF(contentsRect.topLeft() * textureScale,
                                           contentsRect.bottomRight() * textureScale);
            quads << createQuad(contentsRect, sourceRect);
        }
    }

    for (auto child : toplevel->transient()->children) {
        if (!child->transient()->annexed) {
            continue;
        }
        if (child->remnant() && !toplevel->remnant()) {
            // When the child is a remnant but the parent not there is no guarentee the toplevel
            // will become one too what can cause artficats before the child cleanup timer fires.
            continue;
        }
        auto& sw = child->render;
        if (!sw) {
            continue;
        }
        if (auto const pixmap = sw->windowPixmap<window_pixmap>(); !pixmap || !pixmap->isValid()) {
            continue;
        }
        quads << sw->makeContentsQuads(sw->id(), offset + child->pos() - toplevel->pos());
    }

    return quads;
}

void window::invalidateQuadsCache()
{
    cached_quad_list.reset();
}

void window::create_shadow()
{
    auto shadow = create_deco_shadow<render::shadow>(*toplevel);

    if (!shadow && shadow_windowing.create) {
        shadow = shadow_windowing.create(*toplevel);
    }

    if (shadow) {
        updateShadow(shadow);
        Q_EMIT toplevel->shadowChanged();
    }
}

void window::updateShadow(render::shadow* shadow)
{
    if (m_shadow == shadow) {
        return;
    }
    delete m_shadow;
    m_shadow = shadow;
}
int window::x() const
{
    return toplevel->pos().x();
}

int window::y() const
{
    return toplevel->pos().y();
}

int window::width() const
{
    return toplevel->size().width();
}

int window::height() const
{
    return toplevel->size().height();
}

QRect window::geometry() const
{
    return toplevel->frameGeometry();
}

QSize window::size() const
{
    return toplevel->size();
}

QPoint window::pos() const
{
    return toplevel->pos();
}

QRect window::rect() const
{
    return QRect(QPoint(), toplevel->size());
}

Toplevel* window::get_window() const
{
    return toplevel;
}

void window::updateToplevel(Toplevel* c)
{
    toplevel = c;
}

render::shadow const* window::shadow() const
{
    return m_shadow;
}

render::shadow* window::shadow()
{
    return m_shadow;
}

//****************************************
// window_pixmap
//****************************************

window_pixmap::window_pixmap(render::window* window)
    : m_window(window)
    , m_pixmap(XCB_PIXMAP_NONE)
    , m_discarded(false)
{
}

window_pixmap::~window_pixmap()
{
    if (m_pixmap != XCB_WINDOW_NONE) {
        xcb_free_pixmap(connection(), m_pixmap);
    }
}

void window_pixmap::create()
{
    if (isValid() || toplevel()->isDeleted()) {
        return;
    }
    // always update from Buffer on Wayland, don't try using XPixmap
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        // use Buffer
        updateBuffer();
        if (m_buffer || m_fbo) {
            m_window->unreferencePreviousPixmap();
        }
        return;
    }
    base::x11::server_grabber grabber;
    xcb_pixmap_t pix = xcb_generate_id(connection());
    xcb_void_cookie_t namePixmapCookie
        = xcb_composite_name_window_pixmap_checked(connection(), toplevel()->frameId(), pix);
    base::x11::xcb::window_attributes windowAttributes(toplevel()->frameId());

    auto win = toplevel();
    auto xcb_frame_geometry = base::x11::xcb::geometry(win->frameId());

    if (xcb_generic_error_t* error = xcb_request_check(connection(), namePixmapCookie)) {
        qCDebug(KWIN_CORE) << "Creating window pixmap failed: " << error->error_code;
        free(error);
        return;
    }
    // check that the received pixmap is valid and actually matches what we
    // know about the window (i.e. size)
    if (!windowAttributes || windowAttributes->map_state != XCB_MAP_STATE_VIEWABLE) {
        qCDebug(KWIN_CORE) << "Creating window pixmap failed by mapping state: " << win;
        xcb_free_pixmap(connection(), pix);
        return;
    }

    auto const render_geo = win::render_geometry(win);
    if (xcb_frame_geometry.size() != render_geo.size()) {
        qCDebug(KWIN_CORE) << "Creating window pixmap failed by size: " << win << " : "
                           << xcb_frame_geometry.rect() << " | " << render_geo;
        xcb_free_pixmap(connection(), pix);
        return;
    }

    m_pixmap = pix;
    m_pixmapSize = render_geo.size();

    // Content relative to render geometry.
    m_contentsRect = (render_geo - win::frame_margins(win)).translated(-render_geo.topLeft());

    m_window->unreferencePreviousPixmap();
}

bool window_pixmap::isValid() const
{
    if (m_buffer || m_fbo || !m_internalImage.isNull()) {
        return true;
    }
    return m_pixmap != XCB_PIXMAP_NONE;
}

void window_pixmap::updateBuffer()
{
    using namespace Wrapland::Server;
    if (m_window->update_wayland_buffer) {
        m_window->update_wayland_buffer(toplevel(), m_buffer);
    } else if (toplevel()->internalFramebufferObject()) {
        m_fbo = toplevel()->internalFramebufferObject();
    } else if (!toplevel()->internalImageObject().isNull()) {
        m_internalImage = toplevel()->internalImageObject();
    } else {
        m_buffer.reset();
    }
}

Wrapland::Server::Surface* window_pixmap::surface() const
{
    return toplevel()->surface();
}

Wrapland::Server::Buffer* window_pixmap::buffer() const
{
    return m_buffer.get();
}

std::shared_ptr<QOpenGLFramebufferObject> const& window_pixmap::fbo() const
{
    return m_fbo;
}

QImage window_pixmap::internalImage() const
{
    return m_internalImage;
}

Toplevel* window_pixmap::toplevel() const
{
    return m_window->get_window();
}

xcb_pixmap_t window_pixmap::pixmap() const
{
    return m_pixmap;
}

bool window_pixmap::isDiscarded() const
{
    return m_discarded;
}

void window_pixmap::markAsDiscarded()
{
    m_discarded = true;
    m_window->referencePreviousPixmap();
}

const QRect& window_pixmap::contentsRect() const
{
    return m_contentsRect;
}

const QSize& window_pixmap::size() const
{
    return m_pixmapSize;
}

}
