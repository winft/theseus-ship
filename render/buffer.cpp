/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "buffer.h"

#include "base/logging.h"

#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "toplevel.h"
#include "win/geo.h"

namespace KWin::render
{

buffer::buffer(render::window* window)
    : m_window(window)
    , m_pixmap(XCB_PIXMAP_NONE)
    , m_discarded(false)
{
}

buffer::~buffer()
{
    if (m_pixmap != XCB_WINDOW_NONE) {
        xcb_free_pixmap(connection(), m_pixmap);
    }
}

void buffer::create()
{
    if (isValid() || toplevel()->isDeleted()) {
        return;
    }
    // always update from Buffer on Wayland, don't try using XPixmap
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        // use Buffer
        updateBuffer();
        if (m_buffer || m_fbo) {
            m_window->unreference_previous_buffer();
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
        qCDebug(KWIN_CORE) << "Creating buffer failed: " << error->error_code;
        free(error);
        return;
    }
    // check that the received pixmap is valid and actually matches what we
    // know about the window (i.e. size)
    if (!windowAttributes || windowAttributes->map_state != XCB_MAP_STATE_VIEWABLE) {
        qCDebug(KWIN_CORE) << "Creating buffer failed by mapping state: " << win;
        xcb_free_pixmap(connection(), pix);
        return;
    }

    auto const render_geo = win::render_geometry(win);
    if (xcb_frame_geometry.size() != render_geo.size()) {
        qCDebug(KWIN_CORE) << "Creating buffer failed by size: " << win << " : "
                           << xcb_frame_geometry.rect() << " | " << render_geo;
        xcb_free_pixmap(connection(), pix);
        return;
    }

    m_pixmap = pix;
    m_pixmapSize = render_geo.size();

    // Content relative to render geometry.
    m_contentsRect = (render_geo - win::frame_margins(win)).translated(-render_geo.topLeft());

    m_window->unreference_previous_buffer();
}

bool buffer::isValid() const
{
    if (m_buffer || m_fbo || !m_internalImage.isNull()) {
        return true;
    }
    return m_pixmap != XCB_PIXMAP_NONE;
}

void buffer::updateBuffer()
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

Wrapland::Server::Surface* buffer::surface() const
{
    return toplevel()->surface();
}

Wrapland::Server::Buffer* buffer::wayland_buffer() const
{
    return m_buffer.get();
}

std::shared_ptr<QOpenGLFramebufferObject> const& buffer::fbo() const
{
    return m_fbo;
}

QImage buffer::internalImage() const
{
    return m_internalImage;
}

Toplevel* buffer::toplevel() const
{
    return m_window->get_window();
}

xcb_pixmap_t buffer::pixmap() const
{
    return m_pixmap;
}

bool buffer::isDiscarded() const
{
    return m_discarded;
}

void buffer::markAsDiscarded()
{
    m_discarded = true;
    m_window->reference_previous_buffer();
}

const QRect& buffer::contentsRect() const
{
    return m_contentsRect;
}

const QSize& buffer::size() const
{
    return m_pixmapSize;
}

}
