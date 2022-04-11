/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/buffer.h"

#include <xcb/render.h>

namespace KWin::render::xrender
{

class buffer : public render::buffer
{
public:
    buffer(render::window* window, xcb_render_pictformat_t format);
    ~buffer() override;

    xcb_render_picture_t picture() const;
    void create() override;

private:
    xcb_render_picture_t m_picture;
    xcb_render_pictformat_t m_format;
};

}
