/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/outline.h"
#include "xcbutils.h"

namespace KWin::render::backend::x11
{

class non_composited_outline : public outline_visual
{
public:
    non_composited_outline(render::outline* outline);
    ~non_composited_outline() override;
    void show() override;
    void hide() override;

private:
    // TODO: variadic template arguments for adding method arguments
    template<typename T>
    void forEachWindow(T method);
    bool m_initialized;
    Xcb::Window m_topOutline;
    Xcb::Window m_rightOutline;
    Xcb::Window m_bottomOutline;
    Xcb::Window m_leftOutline;
};

template<typename T>
inline void non_composited_outline::forEachWindow(T method)
{
    (m_topOutline.*method)();
    (m_rightOutline.*method)();
    (m_bottomOutline.*method)();
    (m_leftOutline.*method)();
}

}
