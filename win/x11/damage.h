/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/damage.h"

#include <xcb/damage.h>
#include <xcb/xfixes.h>

namespace KWin::win::x11
{

struct damage {
    xcb_damage_damage_t handle{XCB_NONE};
    bool is_reply_pending{false};
    xcb_xfixes_fetch_region_cookie_t region_cookie;
};

template<typename Win>
void damage_handle_notify_event(Win& win)
{
    win.render_data.is_damaged = true;

    if (!win.control) {
        // Note: The region is supposed to specify the damage extents, but we don't know it at
        //       this point. No one who connects to this signal uses the rect however.
        Q_EMIT win.qobject->damaged({});
        return;
    }

    if (win.isWaitingForMoveResizeSync()) {
        return;
    }

    if (!win.render_data.ready_for_painting) {
        // avoid "setReadyForPainting()" function calling overhead
        if (win.sync_request.counter == XCB_NONE) {
            // cannot detect complete redraw, consider done now
            set_ready_for_painting(win);
        }
    }

    Q_EMIT win.qobject->damaged({});
}

/**
 * Resets the damage state and sends a request for the damage region.
 * A call to this function must be followed by a call to getDamageRegionReply(),
 * or the reply will be leaked.
 *
 * Returns true if the window was damaged, and false otherwise.
 */
template<typename Win>
bool damage_reset_and_fetch(Win& win)
{
    if (!win.render_data.is_damaged) {
        return false;
    }

    assert(win.damage.handle != XCB_NONE);

    auto conn = connection();

    // Create a new region and copy the damage region to it,
    // resetting the damaged state.
    xcb_xfixes_region_t region = xcb_generate_id(conn);
    xcb_xfixes_create_region(conn, region, 0, nullptr);
    xcb_damage_subtract(conn, win.damage.handle, 0, region);

    // Send a fetch-region request and destroy the region
    win.damage.region_cookie = xcb_xfixes_fetch_region_unchecked(conn, region);
    xcb_xfixes_destroy_region(conn, region);

    win.render_data.is_damaged = false;
    win.damage.is_reply_pending = true;

    return win.damage.is_reply_pending;
}

/**
 * Gets the reply from a previous call to resetAndFetchDamage().
 * Calling this function is a no-op if there is no pending reply.
 * Call damage() to return the fetched region.
 */
template<typename Win>
void damage_fetch_region_reply(Win& win)
{
    if (!win.damage.is_reply_pending) {
        return;
    }

    win.damage.is_reply_pending = false;

    // Get the fetch-region reply
    auto reply = xcb_xfixes_fetch_region_reply(connection(), win.damage.region_cookie, nullptr);
    if (!reply) {
        return;
    }

    // Convert the reply to a QRegion. The region is relative to the content geometry.
    auto count = xcb_xfixes_fetch_region_rectangles_length(reply);
    QRegion region;

    if (count > 1 && count < 16) {
        auto rects = xcb_xfixes_fetch_region_rectangles(reply);

        QVector<QRect> qrects;
        qrects.reserve(count);

        for (int i = 0; i < count; i++) {
            qrects << QRect(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        }
        region.setRects(qrects.constData(), count);
    } else {
        region += QRect(
            reply->extents.x, reply->extents.y, reply->extents.width, reply->extents.height);
    }

    region.translate(
        -QPoint(win.geo.client_frame_extents.left(), win.geo.client_frame_extents.top()));

    win.render_data.repaints_region |= region;

    if (win.geo.has_in_content_deco) {
        region.translate(-QPoint(left_border(&win), top_border(&win)));
    }

    win.render_data.damage_region |= region;

    free(reply);
}

}
