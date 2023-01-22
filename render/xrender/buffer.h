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
class buffer : public render::buffer<Window>
{
public:
    buffer(Window* window, xcb_render_pictformat_t format)
        : render::buffer<Window>(window)
        , picture{XCB_RENDER_PICTURE_NONE}
        , format{format}
    {
    }

    ~buffer() override
    {
        if (picture != XCB_RENDER_PICTURE_NONE) {
            auto const& win_integrate
                = static_cast<render::x11::buffer_win_integration<render::buffer<Window>>&>(
                    *this->win_integration);
            xcb_render_free_picture(win_integrate.connection, picture);
        }
    }

    void create() override
    {
        if (this->isValid()) {
            return;
        }
        render::buffer<Window>::create();
        if (!this->isValid()) {
            return;
        }

        auto const& win_integrate
            = static_cast<render::x11::buffer_win_integration<render::buffer<Window>>&>(
                *this->win_integration);
        picture = xcb_generate_id(win_integrate.connection);
        xcb_render_create_picture(
            win_integrate.connection, picture, win_integrate.pixmap, format, 0, nullptr);
    }

    xcb_render_picture_t picture;
    xcb_render_pictformat_t format;
};

}
