/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "hide.h"

#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "win/scene.h"

namespace KWin::win::x11
{

template<typename Win>
auto setup_compositing(Win& win)
{
    static_assert(!Win::is_toplevel);

    if (!win::setup_compositing(win, !win.control)) {
        return false;
    }

    if (win.control) {
        // for internalKeep()
        update_visibility(&win);
    }

    return true;
}

template<typename Win>
void update_window_buffer(Win* win)
{
    if (win->render) {
        win->render->update_buffer();
    }
}

template<typename Win, typename BufImpl>
void create_window_buffer(Win* win, BufImpl& buf_impl)
{
    base::x11::server_grabber grabber;
    xcb_pixmap_t pix = xcb_generate_id(connection());
    xcb_void_cookie_t name_cookie
        = xcb_composite_name_window_pixmap_checked(connection(), win->frameId(), pix);
    base::x11::xcb::window_attributes windowAttributes(win->frameId());

    auto xcb_frame_geometry = base::x11::xcb::geometry(win->frameId());

    if (xcb_generic_error_t* error = xcb_request_check(connection(), name_cookie)) {
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

    buf_impl.pixmap = pix;
    buf_impl.size = render_geo.size();

    // Content relative to render geometry.
    buf_impl.contents_rect
        = (render_geo - win::frame_margins(win)).translated(-render_geo.topLeft());
}

}
