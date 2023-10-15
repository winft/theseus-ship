/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/x11/data.h>
#include <win/cursor_shape.h>

#include <xcb/xcb_cursor.h>

namespace KWin::win::x11
{

/**
 * If available always use the cursor_shape variant to avoid cache duplicates for ambiguous cursor
 * names in the non existing cursor name specification
 */
template<typename Space>
xcb_cursor_t xcb_cursor_get(Space& space, std::string const& name)
{
    auto const& x11_data = space.base.x11_data;
    assert(x11_data.connection);

    auto it = space.xcb_cursors.find(name);
    if (it != space.xcb_cursors.end()) {
        return it->second;
    }

    if (name.empty()) {
        return XCB_CURSOR_NONE;
    }

    xcb_cursor_context_t* ctx;
    if (xcb_cursor_context_new(x11_data.connection, base::x11::get_default_screen(x11_data), &ctx)
        < 0) {
        return XCB_CURSOR_NONE;
    }

    xcb_cursor_t xcb_cursor = xcb_cursor_load_cursor(ctx, name.c_str());
    if (xcb_cursor == XCB_CURSOR_NONE) {
        auto const& names = cursor_shape_get_alternative_names(name);
        for (auto const& cursorName : names) {
            xcb_cursor = xcb_cursor_load_cursor(ctx, cursorName.c_str());
            if (xcb_cursor != XCB_CURSOR_NONE) {
                break;
            }
        }
    }
    if (xcb_cursor != XCB_CURSOR_NONE) {
        space.xcb_cursors.insert({name, xcb_cursor});
    }

    xcb_cursor_context_free(ctx);
    return xcb_cursor;
}

template<typename Space>
xcb_cursor_t xcb_cursor_get(Space& space, win::cursor_shape shape)
{
    return xcb_cursor_get(space, shape.name());
}

}
