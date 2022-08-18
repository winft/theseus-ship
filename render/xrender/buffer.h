/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/x11/buffer.h"

#include <xcb/render.h>

namespace KWin::render::xrender
{

template<typename Window>
class buffer : public render::buffer
{
public:
    buffer(Window* window, xcb_render_pictformat_t format)
        : render::buffer(window)
        , picture{XCB_RENDER_PICTURE_NONE}
        , format{format}
    {
    }

    ~buffer() override
    {
        if (picture != XCB_RENDER_PICTURE_NONE) {
            xcb_render_free_picture(connection(), picture);
        }
    }

    void create() override
    {
        if (isValid()) {
            return;
        }
        render::buffer::create();
        if (!isValid()) {
            return;
        }
        picture = xcb_generate_id(connection());
        auto const& win_integrate
            = static_cast<render::x11::buffer_win_integration&>(*win_integration);
        xcb_render_create_picture(connection(), picture, win_integrate.pixmap, format, 0, nullptr);
    }

    xcb_render_picture_t picture;
    xcb_render_pictformat_t format;
};

}
