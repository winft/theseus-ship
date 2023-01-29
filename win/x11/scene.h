/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "hide.h"
#include "window_release.h"

#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "render/x11/buffer.h"
#include "render/x11/shadow.h"
#include "win/geo.h"
#include "win/scene.h"

#include <xcb/damage.h>

namespace KWin::win::x11
{

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
    auto con = win->space.base.x11_data.connection;
    base::x11::server_grabber grabber(con);
    xcb_pixmap_t pix = xcb_generate_id(con);
    xcb_void_cookie_t name_cookie
        = xcb_composite_name_window_pixmap_checked(con, win->frameId(), pix);
    base::x11::xcb::window_attributes windowAttributes(con, win->frameId());

    auto xcb_frame_geometry = base::x11::xcb::geometry(con, win->frameId());

    if (xcb_generic_error_t* error = xcb_request_check(con, name_cookie)) {
        qCDebug(KWIN_CORE) << "Creating buffer failed: " << error->error_code;
        free(error);
        return;
    }

    // check that the received pixmap is valid and actually matches what we
    // know about the window (i.e. size)
    if (!windowAttributes || windowAttributes->map_state != XCB_MAP_STATE_VIEWABLE) {
        qCDebug(KWIN_CORE) << "Creating buffer failed by mapping state: " << win;
        xcb_free_pixmap(con, pix);
        return;
    }

    auto const render_geo = win::render_geometry(win);
    if (xcb_frame_geometry.size() != render_geo.size()) {
        qCDebug(KWIN_CORE) << "Creating buffer failed by size: " << win << " : "
                           << xcb_frame_geometry.rect() << " | " << render_geo;
        xcb_free_pixmap(con, pix);
        return;
    }

    buf_impl.pixmap = pix;
    buf_impl.size = render_geo.size();

    // Content relative to render geometry.
    buf_impl.contents_rect
        = (render_geo - win::frame_margins(win)).translated(-render_geo.topLeft());
}

template<typename Win>
QRegion get_shape_render_region(Win& win)
{
    assert(win.is_shape);

    if (win.is_render_shape_valid) {
        return win.render_shape;
    }

    win.is_render_shape_valid = true;
    win.render_shape = {};

    auto con = win.space.base.x11_data.connection;
    auto cookie = xcb_shape_get_rectangles_unchecked(con, win.frameId(), XCB_SHAPE_SK_BOUNDING);
    unique_cptr<xcb_shape_get_rectangles_reply_t> reply(
        xcb_shape_get_rectangles_reply(con, cookie, nullptr));
    if (!reply) {
        return {};
    }

    auto const rects = xcb_shape_get_rectangles_rectangles(reply.get());
    auto const rect_count = xcb_shape_get_rectangles_rectangles_length(reply.get());
    for (int i = 0; i < rect_count; ++i) {
        win.render_shape += QRegion(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
    }

    // make sure the shape is sane (X is async, maybe even XShape is broken)
    auto const render_geo = render_geometry(&win);
    win.render_shape &= QRegion(0, 0, render_geo.width(), render_geo.height());
    return win.render_shape;
}

template<typename Win>
QRegion get_render_region(Win& win)
{
    if (win.remnant) {
        return win.remnant->data.render_region;
    }

    if (win.is_shape) {
        return get_shape_render_region(win);
    }

    auto const render_geo = win::render_geometry(&win);
    return QRegion(0, 0, render_geo.width(), render_geo.height());
}

template<typename Win>
double get_opacity(Win& win)
{
    if (win.remnant) {
        return win.remnant->data.opacity;
    }
    if (win.net_info->opacity() == 0xffffffff) {
        return 1.0;
    }
    return win.net_info->opacity() * 1.0 / 0xffffffff;
}

template<typename Win>
void set_opacity(Win& win, double new_opacity)
{
    double old_opacity = get_opacity(win);
    new_opacity = qBound(0.0, new_opacity, 1.0);
    if (old_opacity == new_opacity) {
        return;
    }

    win.net_info->setOpacity(static_cast<unsigned long>(new_opacity * 0xffffffff));

    if (win.space.base.render->compositor->scene) {
        add_full_repaint(win);
        Q_EMIT win.qobject->opacityChanged(old_opacity);
    }
}

template<typename Win>
void setup_compositing(Win& win)
{
    assert(!win.remnant);
    assert(win.damage.handle == XCB_NONE);

    if (!win.space.base.render->compositor->scene) {
        return;
    }

    auto con = win.space.base.x11_data.connection;
    win.damage.handle = xcb_generate_id(con);
    xcb_damage_create(con, win.damage.handle, win.frameId(), XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);

    discard_shape(win);
    win.render_data.damage_region = QRect({}, win.geo.size());

    add_scene_window(*win.space.base.render->compositor->scene, win);

    if (win.control) {
        // for internalKeep()
        update_visibility(&win);
    } else {
        // With unmanaged windows there is a race condition between the client painting the window
        // and us setting up damage tracking. If the client wins we won't get a damage event even
        // though the window has been painted. To avoid this we mark the whole window as damaged and
        // schedule a repaint immediately after creating the damage object.
        add_full_damage(win);
    }
}

template<typename Win>
void finish_compositing(Win& win)
{
    win::finish_compositing(win);
    destroy_damage_handle(win);

    // For safety in case KWin is just resizing the window.
    // TODO(romangg): Is this really needed?
    reset_have_resize_effect(win);
}

template<typename Win>
void set_blocking_compositing(Win& win, bool block)
{
    auto const usedToBlock = win.blocks_compositing;
    win.blocks_compositing = win.control->rules.checkBlockCompositing(
        block && win.space.base.options->qobject->windowsBlockCompositing());

    if (usedToBlock != win.blocks_compositing) {
        Q_EMIT win.qobject->blockingCompositingChanged(win.blocks_compositing);
    }
}

template<typename Win>
void add_scene_window_addon(Win& win)
{
    using scene_t = typename Win::space_t::base_t::render_t::compositor_t::scene_t;
    using shadow_t = render::shadow<typename scene_t::window_t>;

    auto& atoms = win.space.atoms;
    win.render->shadow_windowing.create = [&](auto&& render_win) {
        return render::x11::create_shadow<shadow_t, typename scene_t::window_t>(
            render_win, atoms->kde_net_wm_shadow);
    };
    win.render->shadow_windowing.update = [&](auto&& shadow) {
        return render::x11::read_and_update_shadow<shadow_t>(
            shadow, win.space.base.x11_data.connection, atoms->kde_net_wm_shadow);
    };

    auto setup_buffer = [con = win.space.base.x11_data.connection](auto& buffer) {
        using buffer_integration_t
            = render::x11::buffer_win_integration<typename scene_t::buffer_t>;

        auto win_integrate = std::make_unique<buffer_integration_t>(buffer, con);
        auto update_helper = [&buffer]() {
            auto& win_integrate = static_cast<buffer_integration_t&>(*buffer.win_integration);
            create_window_buffer(std::get<Win*>(*buffer.window->ref_win), win_integrate);
        };
        win_integrate->update = update_helper;
        buffer.win_integration = std::move(win_integrate);
    };
    win.render->win_integration.setup_buffer = setup_buffer;
}

template<typename Win>
void fetch_wm_opaque_region(Win& win)
{
    auto const rects = win.net_info->opaqueRegion();
    QRegion new_opaque_region;
    for (const auto& r : rects) {
        new_opaque_region += QRect(r.pos.x, r.pos.y, r.size.width, r.size.height);
    }

    win.render_data.opaque_region = new_opaque_region;
}

}
