/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"

#include "main.h"
#include "utils/geo.h"
#include "utils/memory.h"
#include "win/meta.h"
#include "win/move.h"
#include "win/types.h"

#include "base/x11/grabs.h"

namespace KWin::win::x11
{

template<typename Win>
bool is_movable(Win const& win)
{
    if (!win.net_info->hasNETSupport() && !win.motif_hints.move()) {
        return false;
    }
    if (win.control->fullscreen) {
        return false;
    }
    if (is_special_window(&win) && !is_splash(&win) && !is_toolbar(&win)) {
        // allow moving of splashscreens :)
        return false;
    }
    if (win.control->rules.checkPosition(geo::invalid_point) != geo::invalid_point) {
        // forced position
        return false;
    }
    return true;
}

template<typename Win>
bool is_movable_across_screens(Win const& win)
{
    if (!win.net_info->hasNETSupport() && !win.motif_hints.move()) {
        return false;
    }
    if (is_special_window(&win) && !is_splash(&win) && !is_toolbar(&win)) {
        // allow moving of splashscreens :)
        return false;
    }
    if (win.control->rules.checkPosition(geo::invalid_point) != geo::invalid_point) {
        // forced position
        return false;
    }
    return true;
}

template<typename Win>
bool is_resizable(Win const& win)
{
    if (!win.net_info->hasNETSupport() && !win.motif_hints.resize()) {
        return false;
    }
    if (win.geo.update.fullscreen) {
        return false;
    }
    if (is_special_window(&win) || is_splash(&win) || is_toolbar(&win)) {
        return false;
    }
    if (win.control->rules.checkSize(QSize()).isValid()) {
        // forced size
        return false;
    }

    auto const mode = win.control->move_resize.contact;

    // TODO: we could just check with & on top and left.
    if ((mode == position::top || mode == position::top_left || mode == position::top_right
         || mode == position::left || mode == position::bottom_left)
        && win.control->rules.checkPosition(geo::invalid_point) != geo::invalid_point) {
        return false;
    }

    auto min = win.minSize();
    auto max = win.maxSize();

    return min.width() < max.width() || min.height() < max.height();
}

template<typename Win>
bool do_start_move_resize(Win& win)
{
    bool has_grab = false;

    // This reportedly improves smoothness of the moveresize operation,
    // something with Enter/LeaveNotify events, looks like XFree performance problem or
    // something *shrug* (https://lists.kde.org/?t=107302193400001&r=1&w=2)
    auto r = space_window_area(win.space, FullArea, &win);

    win.xcb_windows.grab.create(r, XCB_WINDOW_CLASS_INPUT_ONLY, 0, nullptr, rootWindow());
    win.xcb_windows.grab.map();
    win.xcb_windows.grab.raise();

    kwinApp()->update_x11_time_from_clock();
    auto const cookie = xcb_grab_pointer_unchecked(
        connection(),
        false,
        win.xcb_windows.grab,
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION
            | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW,
        XCB_GRAB_MODE_ASYNC,
        XCB_GRAB_MODE_ASYNC,
        win.xcb_windows.grab,
        win.space.input->cursor->x11_cursor(win.control->move_resize.cursor),
        xTime());

    unique_cptr<xcb_grab_pointer_reply_t> pointerGrab(
        xcb_grab_pointer_reply(connection(), cookie, nullptr));
    if (pointerGrab && pointerGrab->status == XCB_GRAB_STATUS_SUCCESS) {
        has_grab = true;
    }

    if (!has_grab && base::x11::grab_keyboard(win.frameId()))
        has_grab = win.move_resize_has_keyboard_grab = true;
    if (!has_grab) {
        // at least one grab is necessary in order to be able to finish move/resize
        win.xcb_windows.grab.reset();
        return false;
    }

    return true;
}

template<typename Win>
void leave_move_resize(Win& win)
{
    if (win.move_needs_server_update) {
        // Do the deferred move
        auto const frame_geo = win.geo.frame;
        auto const client_geo = frame_to_client_rect(&win, frame_geo);
        auto const outer_pos = frame_to_render_rect(&win, frame_geo).topLeft();

        win.xcb_windows.outer.move(outer_pos);
        send_synthetic_configure_notify(&win, client_geo);

        win.synced_geometry.frame = frame_geo;
        win.synced_geometry.client = client_geo;

        win.move_needs_server_update = false;
    }

    if (win.move_resize_has_keyboard_grab) {
        base::x11::ungrab_keyboard();
    }

    win.move_resize_has_keyboard_grab = false;
    xcb_ungrab_pointer(connection(), xTime());
    win.xcb_windows.grab.reset();

    win::leave_move_resize(win);
}

template<typename Win>
void do_resize_sync(Win& win)
{
    auto const frame_geo = win.control->move_resize.geometry;

    if (win.sync_request.counter != XCB_NONE) {
        sync_geometry(&win, frame_geo);
        update_server_geometry(&win, frame_geo);
        return;
    }

    // Resizes without sync extension support need to be retarded to not flood clients with
    // geometry changes. Some clients can't handle this (for example Steam client).
    if (!win.syncless_resize_retarder) {
        win.syncless_resize_retarder = new QTimer(win.qobject.get());
        QObject::connect(win.syncless_resize_retarder, &QTimer::timeout, win.qobject.get(), [&win] {
            assert(!win.pending_configures.empty());
            update_server_geometry(&win, win.pending_configures.front().geometry.frame);
            apply_pending_geometry(&win, 0);
        });
        win.syncless_resize_retarder->setSingleShot(true);
    }

    if (win.pending_configures.empty()) {
        assert(!win.syncless_resize_retarder->isActive());
        win.pending_configures.push_back(
            {0, {frame_geo, QRect(), win.geo.update.max_mode, win.geo.update.fullscreen}});
        win.syncless_resize_retarder->start(16);
    } else {
        win.pending_configures.front().geometry.frame = frame_geo;
    }
}

}
