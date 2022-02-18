/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output_helpers.h"
#include "base/platform.h"
#include "main.h"
#include "screens.h"
#include "win/space.h"
#include "win/space_areas.h"
#include "win/virtual_desktops.h"

namespace KWin::win::wayland
{

template<typename Window>
void update_space_areas(Window* win,
                        QRect const& desktop_area,
                        std::vector<QRect> const& screens_geos,
                        space_areas& areas)
{
    auto const& base = kwinApp()->get_base();
    auto const screens_count = base.get_outputs().size();
    auto const desktops_count = static_cast<int>(virtual_desktop_manager::self()->count());

    // Assuming that only docks have "struts" and that all docks have a strut.
    if (!win->hasStrut()) {
        return;
    }
    auto margins = [win](auto const& geometry) {
        QMargins margins;
        if (!geometry.intersects(win->frameGeometry())) {
            return margins;
        }

        // Figure out which areas of the overall screen setup it borders.
        auto const left = win->frameGeometry().left() == geometry.left();
        auto const right = win->frameGeometry().right() == geometry.right();
        auto const top = win->frameGeometry().top() == geometry.top();
        auto const bottom = win->frameGeometry().bottom() == geometry.bottom();
        auto const horizontal = win->frameGeometry().width() >= win->frameGeometry().height();

        if (left && ((!top && !bottom) || !horizontal)) {
            margins.setLeft(win->frameGeometry().width());
        }
        if (right && ((!top && !bottom) || !horizontal)) {
            margins.setRight(win->frameGeometry().width());
        }
        if (top && ((!left && !right) || horizontal)) {
            margins.setTop(win->frameGeometry().height());
        }
        if (bottom && ((!left && !right) || horizontal)) {
            margins.setBottom(win->frameGeometry().height());
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

    auto const strut = margins(win->central_output ? win->central_output->geometry() : QRect());
    auto const strut_region
        = strut_rects{strut_rect(win->frameGeometry(), margins_to_strut_area(strut))};
    auto rect = desktop_area - margins(QRect({}, base.topology.size));

    if (win->isOnAllDesktops()) {
        for (int desktop = 1; desktop <= desktops_count; ++desktop) {
            areas.work[desktop] = areas.work[desktop].intersected(rect);

            for (size_t screen = 0; screen < screens_count; ++screen) {
                auto& screen_area = areas.screen[desktop][screen];
                auto intersect = screens_geos[screen] - margins(screens_geos[screen]);
                screen_area = screen_area.intersected(intersect);
            }

            auto& resmove = areas.restrictedmove[desktop];
            resmove.insert(std::end(resmove), std::begin(strut_region), std::end(strut_region));
        }
    } else {
        areas.work[win->desktop()] = areas.work[win->desktop()].intersected(rect);

        for (size_t screen = 0; screen < screens_count; screen++) {
            areas.screen[win->desktop()][screen] = areas.screen[win->desktop()][screen].intersected(
                screens_geos[screen] - margins(screens_geos[screen]));
        }

        auto& resmove = areas.restrictedmove[win->desktop()];
        resmove.insert(std::end(resmove), std::begin(strut_region), std::end(strut_region));
    }
}

}
