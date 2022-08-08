/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"

#include "base/output.h"
#include "base/output_helpers.h"
#include "base/platform.h"

namespace KWin::win
{

template<typename Win>
void window_setup_geometry(Win& win)
{
    QObject::connect(&win, &Win::frame_geometry_changed, &win, [](auto win, auto const& old_geo) {
        if (render_geometry(win).size() == frame_to_render_rect(win, old_geo).size()) {
            // Size unchanged. No need to update.
            return;
        }
        win->discard_shape();
        Q_EMIT win->visible_geometry_changed();
    });

    QObject::connect(&win, &Win::damaged, &win, &Win::needsRepaint);

    auto& base = kwinApp()->get_base();
    QObject::connect(&base, &base::platform::topology_changed, &win, [&win] { win.checkScreen(); });
    QObject::connect(&base, &base::platform::output_added, &win, [&win](auto output) {
        win.handle_output_added(output);
    });
    QObject::connect(&base, &base::platform::output_removed, &win, [&win](auto output) {
        win.handle_output_removed(output);
    });

    win.setupCheckScreenConnection();
}

}
