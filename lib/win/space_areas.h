/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "strut_rect.h"

#include <QRect>
#include <vector>

namespace KWin::win
{

/// Describes important areas of a space.
struct space_areas {
    space_areas() = default;
    space_areas(size_t count)
        : work{count}
        , restrictedmove{count}
        , screen{count}
    {
    }

    // For each virtual desktop one work area.
    std::vector<QRect> work;

    // For each virtual desktop one area in struc rects that windows cannot be moved into.
    std::vector<strut_rects> restrictedmove;

    // For each virtual desktop an array with one work area per xinerama(?) screen.
    std::vector<std::vector<QRect>> screen;
};

}
