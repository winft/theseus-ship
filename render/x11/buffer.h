/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/buffer.h"

#include <kwinglobals.h>

#include <xcb/xcb.h>

namespace KWin::render::x11
{

template<typename Buffer>
struct buffer_win_integration : public render::buffer_win_integration<Buffer> {
public:
    buffer_win_integration(Buffer const& buffer)
        : render::buffer_win_integration<Buffer>(buffer)
    {
    }
    ~buffer_win_integration() override
    {
        if (pixmap != XCB_WINDOW_NONE) {
            xcb_free_pixmap(connection(), pixmap);
        }
    }

    bool valid() const override
    {
        return pixmap != XCB_PIXMAP_NONE;
    }

    QSize get_size() const override
    {
        return size;
    }

    QRect get_contents_rect() const override
    {
        return contents_rect;
    }

    QRegion damage() const override
    {
        return {};
    }

    xcb_pixmap_t pixmap{XCB_PIXMAP_NONE};
    QSize size;
    QRect contents_rect;
};

}
