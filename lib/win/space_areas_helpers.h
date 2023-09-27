/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "move.h"
#include "space_areas.h"

#include "base/platform.h"

namespace KWin::win
{

/**
 * Updates the current client areas according to the current clients.
 *
 * If the area changes or force is @c true, the new areas are propagated to the world.
 *
 * The client area is the area that is available for clients (that
 * which is not taken by windows like panels, the top-of-screen menu
 * etc).
 *
 * @see clientArea()
 */
template<typename Space>
void update_space_areas_impl(Space& space, bool force)
{
    auto& base = space.base;
    auto const& outputs = base.outputs;
    auto const screens_count = outputs.size();
    auto const desktops_count = static_cast<int>(space.virtual_desktop_manager->count());

    // To be determined are new:
    // * work areas,
    // * restricted-move areas,
    // * screen areas.
    win::space_areas new_areas(desktops_count + 1);

    std::vector<QRect> screens_geos(screens_count);
    QRect desktop_area;

    for (size_t screen = 0; screen < screens_count; screen++) {
        desktop_area |= outputs.at(screen)->geometry();
    }

    for (size_t screen = 0; screen < screens_count; screen++) {
        screens_geos[screen] = outputs.at(screen)->geometry();
    }

    for (auto desktop = 1; desktop <= desktops_count; ++desktop) {
        new_areas.work[desktop] = desktop_area;
        new_areas.screen[desktop].resize(screens_count);
        for (size_t screen = 0; screen < screens_count; screen++) {
            new_areas.screen[desktop][screen] = screens_geos[screen];
        }
    }

    space.update_space_area_from_windows(desktop_area, screens_geos, new_areas);

    auto changed = force || space.areas.screen.empty();

    for (int desktop = 1; !changed && desktop <= desktops_count; ++desktop) {
        changed |= space.areas.work[desktop] != new_areas.work[desktop];
        changed |= space.areas.restrictedmove[desktop] != new_areas.restrictedmove[desktop];
        changed |= space.areas.screen[desktop].size() != new_areas.screen[desktop].size();

        for (size_t screen = 0; !changed && screen < screens_count; screen++) {
            changed |= new_areas.screen[desktop][screen] != space.areas.screen[desktop][screen];
        }
    }

    if (changed) {
        space.oldrestrictedmovearea = space.areas.restrictedmove;
        space.areas = new_areas;

        if constexpr (requires(Space space) { space.update_work_area(); }) {
            space.update_work_area();
        }

        for (auto win : space.windows) {
            std::visit(overload{[&](auto&& win) {
                           if (win->control) {
                               check_workspace_position(win);
                           }
                       }},
                       win);
        }

        // Reset, no longer valid or needed.
        space.oldrestrictedmovearea.clear();
    }
}

template<typename Space>
void update_space_areas(Space& space)
{
    update_space_areas_impl(space, false);
}

template<typename Space>
void reset_space_areas(Space& space, uint desktop_count)
{
    auto& areas = space.areas;

    // Make it +1, so that it can be accessed as [1..numberofdesktops]
    areas.work.clear();
    areas.work.resize(desktop_count + 1);
    areas.restrictedmove.clear();
    areas.restrictedmove.resize(desktop_count + 1);
    areas.screen.clear();

    update_space_areas_impl(space, true);
}

}
