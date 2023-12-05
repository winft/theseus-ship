/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "screen.h"

#include "base/output_helpers.h"

namespace KWin::win
{

template<typename Space>
bool in_update_window_area(Space const& space)
{
    return !space.oldrestrictedmovearea.empty();
}

/**
 * Returns the area available for clients. This is the subspace geometry minus windows on the dock.
 * Placement algorithms should refer to this rather than Screens::geometry.
 */
template<typename Space>
QRect space_window_area(Space const& space,
                        area_option opt,
                        typename Space::base_t::output_t const* output,
                        int subspace)
{
    auto const& outputs = space.base.outputs;

    if (subspace == x11_desktop_number_on_all || subspace == 0) {
        subspace = subspaces_get_current_x11id(*space.subspace_manager);
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
             && output_index < space.areas.screen[subspace].size())
        ? space.areas.screen[subspace][output_index]
        : output_geo;
    warea = space.areas.work[subspace].isNull() ? QRect({}, space.base.topology.size)
                                                : space.areas.work[subspace];

    switch (opt) {
    case area_option::maximize:
    case area_option::placement:
        return sarea;
    case area_option::maximize_full:
    case area_option::fullscreen:
    case area_option::movement:
    case area_option::screen:
        return output_geo;
    case area_option::work:
        return warea;
    case area_option::full:
        return QRect({}, space.base.topology.size);
    }
    abort();
}

template<typename Space>
QRect space_window_area(Space const& space, area_option opt, QPoint const& p, int subspace)
{
    return space_window_area(space, opt, base::get_nearest_output(space.base.outputs, p), subspace);
}

template<typename Space, typename Win>
QRect space_window_area(Space const& space, area_option opt, Win const* window)
{
    return space_window_area(
        space, opt, pending_frame_geometry(window).center(), get_subspace(*window));
}

}
