/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "app_menu.h"
#include "deco.h"
#include "deco/bridge.h"
#include "placement.h"
#include "screen.h"

#include "render/compositor.h"
#include "rules/rule_book.h"

#include <KDecoration2/Decoration>

namespace KWin::win
{

template<typename Win>
void setup_rules(Win* win, bool ignore_temporary)
{
    // TODO(romangg): This disconnects all connections of captionChanged to the window itself.
    //                There is only one so this works fine but it's not robustly specified.
    //                Either reshuffle later or use explicit connection object.
    QObject::disconnect(win, &Win::captionChanged, win, nullptr);
    win->control->set_rules(win->space.rule_book->find(win, ignore_temporary));
    // check only after getting the rules, because there may be a rule forcing window type
    // TODO(romangg): what does this mean?
}

template<typename Win>
void evaluate_rules(Win* win)
{
    setup_rules(win, true);
    win->applyWindowRules();
}

template<typename Space, typename Win>
void setup_space_window_connections(Space* space, Win* win)
{
    // TODO(romangg): Move into a different function about compositor(render) <-> window setup.
    QObject::connect(win, &Win::needsRepaint, &space->render, [win] {
        win->space.render.schedule_repaint(win);
    });
    QObject::connect(win,
                     &Win::desktopPresenceChanged,
                     space->qobject.get(),
                     &Space::qobject_t::desktopPresenceChanged);
    QObject::connect(
        win,
        &Win::minimizedChanged,
        space->qobject.get(),
        std::bind(&Space::qobject_t::clientMinimizedChanged, space->qobject.get(), win));
}

template<typename Win>
void setup_window_control_connections(Win* win)
{
    QObject::connect(win, &Win::clientStartUserMovedResized, win, &Win::moveResizedChanged);
    QObject::connect(win, &Win::clientFinishUserMovedResized, win, &Win::moveResizedChanged);
    QObject::connect(
        win, &Win::clientStartUserMovedResized, win, &Win::removeCheckScreenConnection);
    QObject::connect(
        win, &Win::clientFinishUserMovedResized, win, &Win::setupCheckScreenConnection);

    QObject::connect(win, &Win::paletteChanged, win, [win] { trigger_decoration_repaint(win); });

    QObject::connect(win->space.deco.get(), &QObject::destroyed, win, [win] {
        win->control->destroy_decoration();
    });

    // Replace on-screen-display on size changes.
    QObject::connect(win,
                     &Win::frame_geometry_changed,
                     win,
                     [win]([[maybe_unused]] Toplevel* toplevel, QRect const& old) {
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

                         auto const area = win->space.clientArea(
                             PlacementArea, get_current_output(win->space), win->desktop());

                         win::place(win, area);
                     });

    QObject::connect(
        win->space.app_menu.get(), &app_menu::applicationMenuEnabledChanged, win, [win] {
            Q_EMIT win->hasApplicationMenuChanged(win->control->has_application_menu());
        });
}

}
