/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "netinfo.h"

namespace KWin::win::x11
{

inline void handle_desktop_resize(QSize const& size)
{
    if (!x11::rootInfo()) {
        return;
    }

    NETSize desktop_geometry;
    desktop_geometry.width = size.width();
    desktop_geometry.height = size.height();
    x11::rootInfo()->setDesktopGeometry(desktop_geometry);
}

}
