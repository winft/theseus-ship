/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "buffer.h"

#include <kwinglobals.h>

namespace KWin::render::xrender
{

buffer::buffer(render::window* window, xcb_render_pictformat_t format)
    : render::buffer(window)
    , m_picture(XCB_RENDER_PICTURE_NONE)
    , m_format(format)
{
}

buffer::~buffer()
{
    if (m_picture != XCB_RENDER_PICTURE_NONE) {
        xcb_render_free_picture(connection(), m_picture);
    }
}

void buffer::create()
{
    if (isValid()) {
        return;
    }
    render::buffer::create();
    if (!isValid()) {
        return;
    }
    m_picture = xcb_generate_id(connection());
    xcb_render_create_picture(connection(), m_picture, pixmap(), m_format, 0, nullptr);
}

xcb_render_picture_t buffer::picture() const
{
    return m_picture;
}

}
