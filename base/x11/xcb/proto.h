/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wrapper.h"

#include <xcb/composite.h>
#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

/**
 * Collection of wrappers for functions from the xproto header.
 */

XCB_WRAPPER_DATA(geometry_data, xcb_get_geometry, xcb_drawable_t)
class geometry : public wrapper<geometry_data, xcb_window_t>
{
public:
    geometry()
        : wrapper<geometry_data, xcb_window_t>()
    {
    }
    explicit geometry(xcb_window_t window)
        : wrapper<geometry_data, xcb_window_t>(window)
    {
    }

    inline QRect rect()
    {
        const xcb_get_geometry_reply_t* geometry = data();
        if (!geometry) {
            return QRect();
        }
        return QRect(geometry->x, geometry->y, geometry->width, geometry->height);
    }

    inline QSize size()
    {
        const xcb_get_geometry_reply_t* geometry = data();
        if (!geometry) {
            return QSize();
        }
        return QSize(geometry->width, geometry->height);
    }
};

struct input_focus_data
    : public wrapper_data<xcb_get_input_focus_reply_t, xcb_get_input_focus_cookie_t> {
    static constexpr request_func requestFunc = &xcb_get_input_focus_unchecked;
    static constexpr reply_func replyFunc = &xcb_get_input_focus_reply;
};

class input_focus : public wrapper<input_focus_data>
{
public:
    input_focus()
        : wrapper<input_focus_data>()
    {
    }

    inline xcb_window_t window()
    {
        if (is_null()) {
            return XCB_WINDOW_NONE;
        }
        return (*this)->focus;
    }
};

struct modifier_mapping_data
    : public wrapper_data<xcb_get_modifier_mapping_reply_t, xcb_get_modifier_mapping_cookie_t> {
    static constexpr request_func requestFunc = &xcb_get_modifier_mapping_unchecked;
    static constexpr reply_func replyFunc = &xcb_get_modifier_mapping_reply;
};

class modifier_mapping : public wrapper<modifier_mapping_data>
{
public:
    modifier_mapping()
        : wrapper<modifier_mapping_data>()
    {
    }

    inline xcb_keycode_t* keycodes()
    {
        if (is_null()) {
            return nullptr;
        }
        return xcb_get_modifier_mapping_keycodes(data());
    }
    inline int size()
    {
        if (is_null()) {
            return 0;
        }
        return xcb_get_modifier_mapping_keycodes_length(data());
    }
};

XCB_WRAPPER(overlay_window, xcb_composite_get_overlay_window, xcb_window_t)

XCB_WRAPPER(pointer, xcb_query_pointer, xcb_window_t)

struct query_keymap_data
    : public wrapper_data<xcb_query_keymap_reply_t, xcb_query_keymap_cookie_t> {
    static constexpr request_func requestFunc = &xcb_query_keymap_unchecked;
    static constexpr reply_func replyFunc = &xcb_query_keymap_reply;
};

class query_keymap : public wrapper<query_keymap_data>
{
public:
    query_keymap()
        : wrapper<query_keymap_data>()
    {
    }
};

XCB_WRAPPER_DATA(tree_data, xcb_query_tree, xcb_window_t)
class tree : public wrapper<tree_data, xcb_window_t>
{
public:
    explicit tree(xcb_window_t window)
        : wrapper<tree_data, xcb_window_t>(window)
    {
    }

    inline xcb_window_t* children()
    {
        if (is_null() || data()->children_len == 0) {
            return nullptr;
        }
        return xcb_query_tree_children(data());
    }
    inline xcb_window_t parent()
    {
        if (is_null()) {
            return XCB_WINDOW_NONE;
        }
        return (*this)->parent;
    }
};

XCB_WRAPPER(window_attributes, xcb_get_window_attributes, xcb_window_t)

}
