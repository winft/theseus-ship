/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "backend.h"

#include "base/logging.h"
#include "base/platform.h"
#include "base/x11/xcb/extensions.h"
#include "base/x11/xcb/helpers.h"
#include "main.h"
#include "render/x11/compositor.h"
#include "render/x11/overlay_window.h"
#include "win/space.h"

#include <kwinxrender/utils.h>
#include <stdexcept>

namespace KWin::render::xrender
{

backend::backend(x11::compositor<x11::platform>& compositor)
    : overlay_window{std::make_unique<render::x11::overlay_window>(compositor)}
{
    if (!base::x11::xcb::extensions::self()->is_render_available()) {
        throw std::runtime_error("No XRender extension available");
    }
    if (!base::x11::xcb::extensions::self()->is_fixes_region_available()) {
        throw std::runtime_error("No XFixes v3+ extension available");
    }

    compositor.overlay_window = overlay_window.get();
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
        unique_cptr<xcb_get_window_attributes_reply_t> attribs(xcb_get_window_attributes_reply(
            connection(),
            xcb_get_window_attributes_unchecked(connection(), overlay_window->window()),
            nullptr));
        if (!attribs) {
            throw std::runtime_error("Failed getting window attributes for overlay window");
        }
        m_format = XRenderUtils::findPictFormat(attribs->visual);
        if (m_format == 0) {
            throw std::runtime_error("Failed to find XRender format for overlay window");
        }
        m_front = xcb_generate_id(connection());
        xcb_render_create_picture(
            connection(), m_front, overlay_window->window(), m_format, 0, nullptr);
    } else {
        // create XRender picture for the root window
        m_format = XRenderUtils::findPictFormat(defaultScreen()->root_visual);
        if (m_format == 0) {
            throw std::runtime_error("Failed to find XRender format for root window");
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
    auto const& space_size = kwinApp()->get_base().topology.size;
    xcb_create_pixmap(connection(),
                      base::x11::xcb::default_depth(kwinApp()->x11ScreenNumber()),
                      pixmap,
                      rootWindow(),
                      space_size.width(),
                      space_size.height());
    xcb_render_picture_t b = xcb_generate_id(connection());
    xcb_render_create_picture(connection(), b, pixmap, m_format, 0, nullptr);
    xcb_free_pixmap(connection(), pixmap); // The picture owns the pixmap now
    setBuffer(b);
}

void backend::present(paint_type mask, QRegion const& damage)
{
    auto const& space_size = kwinApp()->get_base().topology.size;

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
                             space_size.width(),
                             space_size.height());
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
                             space_size.width(),
                             space_size.height());
        xcb_flush(connection());
    }
}

void backend::screenGeometryChanged(QSize const& size)
{
    overlay_window->resize(size);
    init(false);
}

}
