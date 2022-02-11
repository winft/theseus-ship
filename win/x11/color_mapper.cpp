/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "color_mapper.h"

#include "window.h"

#include "kwinglobals.h"

namespace KWin::win::x11
{

color_mapper::color_mapper(QObject* parent)
    : QObject(parent)
    , m_default(defaultScreen()->default_colormap)
    , m_installed(defaultScreen()->default_colormap)
{
}

color_mapper::~color_mapper() = default;

void color_mapper::update()
{
    auto cmap = m_default;
    if (auto window = dynamic_cast<x11::window*>(workspace()->activeClient())) {
        if (window->colormap != XCB_COLORMAP_NONE) {
            cmap = window->colormap;
        }
    }
    if (cmap != m_installed) {
        xcb_install_colormap(connection(), cmap);
        m_installed = cmap;
    }
}

}
