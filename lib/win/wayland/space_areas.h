/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output_helpers.h"
#include "win/desktop_get.h"
#include "win/space_areas.h"

namespace KWin::win::wayland
{

template<typename Window>
void update_space_areas(Window* win,
                        QRect const& desktop_area,
                        std::vector<QRect> const& screens_geos,
                        space_areas& areas)
{
    auto const screens_count = win->space.base.outputs.size();
    auto const subspaces_count = static_cast<int>(win->space.subspace_manager->count());

    // Assuming that only docks have "struts" and that all docks have a strut.
    if (!win->hasStrut()) {
        return;
    }
    auto margins = [win](auto const& geometry) {
        QMargins margins;
        if (!geometry.intersects(win->geo.frame)) {
            return margins;
        }

        // Figure out which areas of the overall screen setup it borders.
        auto const left = win->geo.frame.left() == geometry.left();
        auto const right = win->geo.frame.right() == geometry.right();
        auto const top = win->geo.frame.top() == geometry.top();
        auto const bottom = win->geo.frame.bottom() == geometry.bottom();
        auto const horizontal = win->geo.frame.width() >= win->geo.frame.height();

        if (left && ((!top && !bottom) || !horizontal)) {
            margins.setLeft(win->geo.frame.width());
        }
        if (right && ((!top && !bottom) || !horizontal)) {
            margins.setRight(win->geo.frame.width());
        }
        if (top && ((!left && !right) || horizontal)) {
            margins.setTop(win->geo.frame.height());
        }
        if (bottom && ((!left && !right) || horizontal)) {
            margins.setBottom(win->geo.frame.height());
        }
        return margins;
    };

    auto margins_to_strut_area = [](auto const& margins) {
        if (margins.left() != 0) {
            return strut_area::left;
        }
        if (margins.right() != 0) {
            return strut_area::right;
        }
        if (margins.top() != 0) {
            return strut_area::top;
        }
        if (margins.bottom() != 0) {
            return strut_area::bottom;
        }
        return strut_area::invalid;
    };

    auto const strut
        = margins(win->topo.central_output ? win->topo.central_output->geometry() : QRect());
    auto const strut_region = strut_rects{strut_rect(win->geo.frame, margins_to_strut_area(strut))};
    auto rect = desktop_area - margins(QRect({}, win->space.base.topology.size));

    if (on_all_subspaces(*win)) {
        for (int sub = 1; sub <= subspaces_count; ++sub) {
            areas.work[sub] = areas.work[sub].intersected(rect);

            for (size_t screen = 0; screen < screens_count; ++screen) {
                auto& screen_area = areas.screen[sub][screen];
                auto intersect = screens_geos[screen] - margins(screens_geos[screen]);
                screen_area = screen_area.intersected(intersect);
            }

            auto& resmove = areas.restrictedmove[sub];
            resmove.insert(std::end(resmove), std::begin(strut_region), std::end(strut_region));
        }
    } else {
        auto subspace = get_subspace(*win);
        areas.work[subspace] = areas.work[subspace].intersected(rect);

        for (size_t screen = 0; screen < screens_count; screen++) {
            areas.screen[subspace][screen] = areas.screen[subspace][screen].intersected(
                screens_geos[screen] - margins(screens_geos[screen]));
        }

        auto& resmove = areas.restrictedmove[subspace];
        resmove.insert(std::end(resmove), std::begin(strut_region), std::end(strut_region));
    }
}

}
