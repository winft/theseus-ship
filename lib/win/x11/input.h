/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net/net.h"
#include "types.h"

#include "base/options.h"
#include "base/x11/xcb/extensions.h"
#include "base/x11/xcb/helpers.h"
#include "hidden_preview.h"
#include "win/deco.h"

#include <QRect>
#include <xcb/shape.h>

namespace KWin::win::x11
{

template<typename Win>
bool wants_input(Win const& win)
{
    return win.control->rules.checkAcceptFocus(
        win.acceptsFocus() || win.net_info->supportsProtocol(net::TakeFocusProtocol));
}

template<typename Win>
void update_input_window(Win* win, QRect const& frame_geo)
{
    static_assert(!Win::is_toplevel);

    if (!base::x11::xcb::extensions::self()->is_shape_input_available()) {
        return;
    }

    QRegion region;

    auto const has_border = !win->user_no_border && !win->geo.update.fullscreen;

    if (has_border && win::decoration(win)) {
        auto const& borders = win::decoration(win)->resizeOnlyBorders();
        auto const left = borders.left();
        auto const top = borders.top();
        auto const right = borders.right();
        auto const bottom = borders.bottom();
        if (left != 0 || top != 0 || right != 0 || bottom != 0) {
            region = QRegion(-left,
                             -top,
                             win::decoration(win)->size().width() + left + right,
                             win::decoration(win)->size().height() + top + bottom);
            region = region.subtracted(win::decoration(win)->rect());
        }
    }

    if (region.isEmpty()) {
        win->xcb_windows.input.reset();
        return;
    }

    auto bounds = region.boundingRect();
    win->input_offset = bounds.topLeft();

    // Move the bounding rect to screen coordinates
    bounds.translate(frame_geo.topLeft());

    // Move the region to input window coordinates
    region.translate(-win->input_offset);

    if (!win->xcb_windows.input.is_valid()) {
        auto const mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
        uint32_t const values[] = {true,
                                   XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                                       | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                                       | XCB_EVENT_MASK_POINTER_MOTION};
        win->xcb_windows.input.create(win->space.base.x11_data.connection,
                                      win->space.base.x11_data.root_window,
                                      bounds,
                                      XCB_WINDOW_CLASS_INPUT_ONLY,
                                      mask,
                                      values);
        if (win->mapping == mapping_state::mapped) {
            win->xcb_windows.input.map();
        }
    } else {
        win->xcb_windows.input.set_geometry(bounds);
    }

    auto const rects = base::x11::xcb::qt_region_to_rects(region);
    xcb_shape_rectangles(win->space.base.x11_data.connection,
                         XCB_SHAPE_SO_SET,
                         XCB_SHAPE_SK_INPUT,
                         XCB_CLIP_ORDERING_UNSORTED,
                         win->xcb_windows.input,
                         0,
                         0,
                         rects.count(),
                         rects.constData());
}

template<typename Space>
void mark_as_user_interaction(Space& space)
{
    if (space.was_user_interaction) {
        return;
    }

    space.was_user_interaction = true;

    // might be called from within the filter, so delay till we now the filter returned
    QTimer::singleShot(0, space.qobject.get(), [&] { space.m_wasUserInteractionFilter.reset(); });
}

template<typename Win>
void update_input_shape(Win& win)
{
    if (win.mapping == mapping_state::kept) {
        // Sets it to none, don't change.
        return;
    }

    if (!base::x11::xcb::extensions::self()->is_shape_input_available()) {
        return;
    }
    // There appears to be no way to find out if a window has input
    // shape set or not, so always propagate the input shape
    // (it's the same like the bounding shape by default).
    // Also, build the shape using a helper window, not directly
    // in the frame window, because the sequence set-shape-to-frame,
    // remove-shape-of-client, add-input-shape-of-client has the problem
    // that after the second step there's a hole in the input shape
    // until the real shape of the client is added and that can make
    // the window lose focus (which is a problem with mouse focus policies)
    // TODO: It seems there is, after all - XShapeGetRectangles() - but maybe this is better
    if (auto& shape_helper = win.space.shape_helper_window; !shape_helper.is_valid()) {
        auto& x11_data = win.space.base.x11_data;
        shape_helper.create(x11_data.connection, x11_data.root_window, QRect(0, 0, 1, 1));
    }

    win.space.shape_helper_window.resize(render_geometry(&win).size());
    auto const deco_margin = QPoint(left_border(&win), top_border(&win));

    auto con = win.space.base.x11_data.connection;

    xcb_shape_combine(con,
                      XCB_SHAPE_SO_SET,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_BOUNDING,
                      win.space.shape_helper_window,
                      0,
                      0,
                      win.frameId());
    xcb_shape_combine(con,
                      XCB_SHAPE_SO_SUBTRACT,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_BOUNDING,
                      win.space.shape_helper_window,
                      deco_margin.x(),
                      deco_margin.y(),
                      win.xcb_windows.client);
    xcb_shape_combine(con,
                      XCB_SHAPE_SO_UNION,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_INPUT,
                      win.space.shape_helper_window,
                      deco_margin.x(),
                      deco_margin.y(),
                      win.xcb_windows.client);
    xcb_shape_combine(con,
                      XCB_SHAPE_SO_SET,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_INPUT,
                      win.frameId(),
                      0,
                      0,
                      win.space.shape_helper_window);
}

}
