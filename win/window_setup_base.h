/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "scene.h"

#include "base/output.h"
#include "base/output_helpers.h"
#include "base/platform.h"

namespace KWin::win
{

template<typename Win>
void window_setup_geometry(Win& win)
{
    QObject::connect(win.qobject.get(),
                     &Win::qobject_t::frame_geometry_changed,
                     win.qobject.get(),
                     [&win](auto const& old_geo) {
                         if (render_geometry(&win).size()
                             == frame_to_render_rect(&win, old_geo).size()) {
                             // Size unchanged. No need to update.
                             return;
                         }
                         discard_shape(win);
                         Q_EMIT win.qobject->visible_geometry_changed();
                     });

    QObject::connect(win.qobject.get(),
                     &Win::qobject_t::damaged,
                     win.qobject.get(),
                     &Win::qobject_t::needsRepaint);

    auto& base = kwinApp()->get_base();
    QObject::connect(
        &base, &base::platform::topology_changed, win.qobject.get(), [&win] { win.checkScreen(); });
    QObject::connect(&base, &base::platform::output_added, win.qobject.get(), [&win](auto output) {
        win.handle_output_added(static_cast<typename Win::output_t*>(output));
    });
    QObject::connect(
        &base, &base::platform::output_removed, win.qobject.get(), [&win](auto output) {
            win.handle_output_removed(static_cast<typename Win::output_t*>(output));
        });

    win.setupCheckScreenConnection();
}

}
