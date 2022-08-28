/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/shadow.h"

#include <kwinxrender/utils.h>

#include <xcb/render.h>

namespace KWin::render
{

class compositor;

namespace xrender
{

class shadow : public render::shadow
{
public:
    explicit shadow(render::window* window);
    ~shadow() override;

    void layoutShadowRects(QRect& top,
                           QRect& topRight,
                           QRect& right,
                           QRect& bottomRight,
                           QRect& bottom,
                           QRect& bottomLeft,
                           QRect& Left,
                           QRect& topLeft);
    xcb_render_picture_t picture(shadow_element element) const;

protected:
    void buildQuads() override;
    bool prepareBackend() override;

private:
    XRenderPicture* m_pictures[static_cast<int>(shadow_element::count)];
};

}
}
