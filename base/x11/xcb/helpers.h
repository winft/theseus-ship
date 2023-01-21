/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"
#include "utils/memory.h"

#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QVector>
#include <vector>
#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

inline void move_resize_window(xcb_connection_t* con, xcb_window_t window, QRect const& geometry)
{
    const uint16_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH
        | XCB_CONFIG_WINDOW_HEIGHT;
    const uint32_t values[] = {static_cast<uint32_t>(geometry.x()),
                               static_cast<uint32_t>(geometry.y()),
                               static_cast<uint32_t>(geometry.width()),
                               static_cast<uint32_t>(geometry.height())};
    xcb_configure_window(con, window, mask, values);
}

inline void move_window(xcb_connection_t* con, xcb_window_t window, uint32_t x, uint32_t y)
{
    const uint16_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    const uint32_t values[] = {x, y};
    xcb_configure_window(con, window, mask, values);
}

inline void move_window(xcb_connection_t* con, xcb_window_t window, QPoint const& pos)
{
    move_window(con, window, pos.x(), pos.y());
}

inline void lower_window(xcb_connection_t* con, xcb_window_t window)
{
    const uint32_t values[] = {XCB_STACK_MODE_BELOW};
    xcb_configure_window(con, window, XCB_CONFIG_WINDOW_STACK_MODE, values);
}

inline xcb_window_t create_input_window(xcb_connection_t* con,
                                        xcb_window_t root_window,
                                        QRect const& geometry,
                                        uint32_t mask,
                                        uint32_t const* values)
{
    auto window = xcb_generate_id(con);
    xcb_create_window(con,
                      0,
                      window,
                      root_window,
                      geometry.x(),
                      geometry.y(),
                      geometry.width(),
                      geometry.height(),
                      0,
                      XCB_WINDOW_CLASS_INPUT_ONLY,
                      XCB_COPY_FROM_PARENT,
                      mask,
                      values);
    return window;
}

inline void restack_windows(xcb_connection_t* con, std::vector<xcb_window_t> const& windows)
{
    if (windows.size() < 2) {
        // only one window, nothing to do
        return;
    }
    for (size_t i = 1; i < windows.size(); ++i) {
        const uint16_t mask = XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE;
        const uint32_t stackingValues[] = {windows.at(i - 1), XCB_STACK_MODE_BELOW};
        xcb_configure_window(con, windows.at(i), mask, stackingValues);
    }
}

inline void restack_windows_with_raise(xcb_connection_t* con,
                                       std::vector<xcb_window_t> const& windows)
{
    if (windows.empty()) {
        return;
    }
    const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(con, windows.front(), XCB_CONFIG_WINDOW_STACK_MODE, values);
    restack_windows(con, windows);
}

inline int default_depth(xcb_connection_t* con, int screen)
{
    static int depth = 0;
    if (depth != 0) {
        return depth;
    }
    for (xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(con)); it.rem;
         --screen, xcb_screen_next(&it)) {
        if (screen == 0) {
            depth = it.data->root_depth;
            break;
        }
    }
    return depth;
}

inline xcb_rectangle_t qt_rect_to_rect(QRect const& rect)
{
    xcb_rectangle_t rectangle;
    rectangle.x = rect.x();
    rectangle.y = rect.y();
    rectangle.width = rect.width();
    rectangle.height = rect.height();
    return rectangle;
}

inline QVector<xcb_rectangle_t> qt_region_to_rects(QRegion const& region)
{
    QVector<xcb_rectangle_t> rects;
    rects.reserve(region.rectCount());
    for (const QRect& rect : region) {
        rects.append(xcb::qt_rect_to_rect(rect));
    }
    return rects;
}

inline void define_cursor(xcb_connection_t* con, xcb_window_t window, xcb_cursor_t cursor)
{
    xcb_change_window_attributes(con, window, XCB_CW_CURSOR, &cursor);
}

inline void
set_transient_for(xcb_connection_t* con, xcb_window_t window, xcb_window_t transient_for_window)
{
    xcb_change_property(con,
                        XCB_PROP_MODE_REPLACE,
                        window,
                        XCB_ATOM_WM_TRANSIENT_FOR,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &transient_for_window);
}

inline void sync(xcb_connection_t* con)
{
    const auto cookie = xcb_get_input_focus(con);
    xcb_generic_error_t* error = nullptr;
    unique_cptr<xcb_get_input_focus_reply_t> sync(xcb_get_input_focus_reply(con, cookie, &error));
    if (error) {
        free(error);
    }
}

inline void select_input(xcb_connection_t* con, xcb_window_t window, uint32_t events)
{
    xcb_change_window_attributes(con, window, XCB_CW_EVENT_MASK, &events);
}

}
