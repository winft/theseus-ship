/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "screen.h"

#include "base/output_helpers.h"
#include "base/platform.h"
#include "kwinglobals.h"

#include <NETWM>

namespace KWin::win
{

template<typename Space>
bool in_update_window_area(Space const& space)
{
    return !space.oldrestrictedmovearea.empty();
}

/**
 * Returns the area available for clients. This is the desktop
 * geometry minus windows on the dock. Placement algorithms should
 * refer to this rather than Screens::geometry.
 */
template<typename Space>
QRect space_window_area(Space const& space,
                        clientAreaOption opt,
                        typename Space::base_t::output_t const* output,
                        int desktop)
{
    auto const& outputs = space.base.outputs;

    if (desktop == NETWinInfo::OnAllDesktops || desktop == 0) {
        desktop = space.virtual_desktop_manager->current();
    }
    if (!output) {
        output = get_current_output(space);
    }

    QRect output_geo;
    size_t output_index{0};

    if (output) {
        output_geo = output->geometry();
        output_index = base::get_output_index(outputs, *output);
    }

    QRect sarea, warea;
    sarea = (!space.areas.screen.empty()
             // screens may be missing during KWin initialization or screen config changes
             && output_index < space.areas.screen[desktop].size())
        ? space.areas.screen[desktop][output_index]
        : output_geo;
    warea = space.areas.work[desktop].isNull() ? QRect({}, space.base.topology.size)
                                               : space.areas.work[desktop];

    switch (opt) {
    case MaximizeArea:
    case PlacementArea:
        return sarea;
    case MaximizeFullArea:
    case FullScreenArea:
    case MovementArea:
    case ScreenArea:
        return output_geo;
    case WorkArea:
        return warea;
    case FullArea:
        return QRect({}, space.base.topology.size);
    }
    abort();
}

template<typename Space>
QRect space_window_area(Space const& space, clientAreaOption opt, QPoint const& p, int desktop)
{
    return space_window_area(space, opt, base::get_nearest_output(space.base.outputs, p), desktop);
}

template<typename Space, typename Win>
QRect space_window_area(Space const& space, clientAreaOption opt, Win const* window)
{
    return space_window_area(
        space, opt, pending_frame_geometry(window).center(), get_desktop(*window));
}

}
