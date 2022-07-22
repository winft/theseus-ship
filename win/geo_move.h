/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "strut_rect.h"
#include "types.h"

#include <NETWM>
#include <QRegion>

namespace KWin::win
{

template<typename Space>
QRegion struts_to_region(Space const& space,
                         int desktop,
                         win::strut_area areas,
                         std::vector<win::strut_rects> const& struts)
{
    if (desktop == NETWinInfo::OnAllDesktops || desktop == 0) {
        desktop = space.virtual_desktop_manager->current();
    }

    QRegion region;
    auto const& rects = struts[desktop];

    for (auto const& rect : rects) {
        if (flags(areas & rect.area())) {
            region += rect;
        }
    }

    return region;
}

template<typename Space>
QRegion restricted_move_area(Space const& space, int desktop, win::strut_area areas)
{
    return struts_to_region(space, desktop, areas, space.areas.restrictedmove);
}

template<typename Space>
QRegion previous_restricted_move_area(Space const& space, int desktop, win::strut_area areas)
{
    return struts_to_region(space, desktop, areas, space.oldrestrictedmovearea);
}

}
