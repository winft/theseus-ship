/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "dbus/appmenu.h"
#include "deco.h"
#include "deco/bridge.h"
#include "placement.h"
#include "screen.h"

#include <KDecoration2/Decoration>

namespace KWin::win
{

template<typename Space, typename Win>
void setup_space_window_connections(Space* space, Win* win)
{
    // TODO(romangg): Move into a different function about compositor(render) <-> window setup.
    QObject::connect(win->qobject.get(),
                     &window_qobject::needsRepaint,
                     space->base.render->compositor->qobject.get(),
                     [win] { win->space.base.render->compositor->schedule_repaint(win); });
    QObject::connect(win->qobject.get(),
                     &window_qobject::desktopPresenceChanged,
                     space->qobject.get(),
                     [space, win](auto desktop) {
                         Q_EMIT space->qobject->desktopPresenceChanged(win->signal_id, desktop);
                     });
    QObject::connect(
        win->qobject.get(), &window_qobject::minimizedChanged, space->qobject.get(), [space, win] {
            Q_EMIT space->qobject->clientMinimizedChanged(win->signal_id);
        });
}

template<typename Win>
void setup_window_control_connections(Win* win)
{
    auto qtwin = win->qobject.get();

    QObject::connect(qtwin,
                     &window_qobject::clientStartUserMovedResized,
                     qtwin,
                     &window_qobject::moveResizedChanged);
    QObject::connect(qtwin,
                     &window_qobject::clientFinishUserMovedResized,
                     qtwin,
                     &window_qobject::moveResizedChanged);
    QObject::connect(qtwin, &window_qobject::clientStartUserMovedResized, qtwin, [win] {
        win->removeCheckScreenConnection();
    });
    QObject::connect(qtwin, &window_qobject::clientFinishUserMovedResized, qtwin, [win] {
        win->setupCheckScreenConnection();
    });

    QObject::connect(
        qtwin, &window_qobject::paletteChanged, qtwin, [win] { trigger_decoration_repaint(win); });

    QObject::connect(win->space.deco.get(), &QObject::destroyed, qtwin, [win] {
        win->control->destroy_decoration();
    });

    // Replace on-screen-display on size changes.
    QObject::connect(qtwin, &window_qobject::frame_geometry_changed, qtwin, [win](auto const& old) {
        if (!is_on_screen_display(win)) {
            // Not an on-screen-display.
            return;
        }
        if (win->frameGeometry().isEmpty()) {
            // No current geometry to set.
            return;
        }
        if (old.size() == win->frameGeometry().size()) {
            // No change.
            return;
        }
        if (win->isInitialPositionSet()) {
            // Position (geometry?) already set.
            return;
        }

        geometry_updates_blocker blocker(win);
        auto const area = space_window_area(
            win->space, PlacementArea, get_current_output(win->space), win->desktop());
        win::place(win, area);
    });

    QObject::connect(
        win->space.appmenu.get(), &dbus::appmenu::applicationMenuEnabledChanged, qtwin, [win] {
            Q_EMIT win->qobject->hasApplicationMenuChanged(win->control->has_application_menu());
        });
}

}
