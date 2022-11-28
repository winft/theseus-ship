/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <QObject>
#include <xcb/xproto.h>

namespace KWin::win::x11
{

template<typename Space>
class color_mapper : public QObject
{
public:
    explicit color_mapper(Space& space)
        : m_default(defaultScreen()->default_colormap)
        , m_installed(defaultScreen()->default_colormap)
        , space{space}
    {
    }

    void update()
    {
        auto cmap = m_default;
        if (auto& win = space.stacking.active) {
            std::visit(overload{[&](typename Space::x11_window* win) {
                                    if (win->colormap != XCB_COLORMAP_NONE) {
                                        cmap = win->colormap;
                                    }
                                },
                                [](auto&&) {}},
                       *win);
        }
        if (cmap != m_installed) {
            xcb_install_colormap(connection(), cmap);
            m_installed = cmap;
        }
    }

private:
    xcb_colormap_t m_default;
    xcb_colormap_t m_installed;
    Space& space;
};

}