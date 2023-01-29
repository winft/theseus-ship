/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 1997 to 2002 Cristian Tibirna <tibirna@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include "types.h"

#include "base/options.h"
#include "input/cursor.h"
#include "kwinglobals.h"
#include "rules/ruling.h"

#include "geo_change.h"
#include "meta.h"
#include "move.h"
#include "net.h"
#include "stacking_order.h"
#include "transient.h"
#include "window_area.h"

#include <QList>
#include <QPoint>
#include <QRect>

namespace KWin::win
{

/**
 * Place the client \a c according to a simply "random" placement algorithm.
 */
template<typename Win>
void place_at_random(Win* window, QRect const& area)
{
    assert(area.isValid());

    int const step = 24;
    static int px = step;
    static int py = 2 * step;
    int tx;
    int ty;

    px = std::max(px, area.x());
    py = std::max(py, area.y());

    px += step;
    py += 2 * step;

    if (px > area.width() / 2) {
        px = area.x() + step;
    }
    if (py > area.height() / 2) {
        py = area.y() + step;
    }

    tx = px;
    ty = py;

    if (tx + window->geo.update.frame.size().width() > area.right()) {
        tx = std::max(area.right() - window->geo.update.frame.size().width(), 0);
        px = area.x();
    }
    if (ty + window->geo.update.frame.size().height() > area.bottom()) {
        ty = std::max(area.bottom() - window->geo.update.frame.size().height(), 0);
        py = area.y();
    }

    move(window, QPoint(tx, ty));
}

/**
 * Place the client \a c according to a really smart placement algorithm :-)
 */
template<typename Win>
void place_smart(Win* window, QRect const& area)
{
    assert(area.isValid());

    /*
     * SmartPlacement by Cristian Tibirna (tibirna@kde.org)
     * adapted for kwm (16-19jan98) and for kwin (16Nov1999) using (with
     * permission) ideas from fvwm, authored by
     * Anthony Martin (amartin@engr.csulb.edu).
     * Xinerama supported added by Balaji Ramani (balaji@yablibli.com)
     * with ideas from xfce.
     */

    if (!window->geo.update.frame.size().isValid()) {
        return;
    }

    // overlap types
    int const none = 0;
    int const h_wrong = -1;
    int const w_wrong = -2;

    long int overlap = 0;
    long int min_overlap = 0;

    int x_optimal;
    int y_optimal;

    int possible;
    int desktop = get_desktop(*window) == 0 || on_all_desktops(window)
        ? window->space.virtual_desktop_manager->current()
        : get_desktop(*window);

    // temp coords
    int cxl;
    int cxr;
    int cyt;
    int cyb;
    int xl;
    int xr;
    int yt;
    int yb;

    // temp holder
    int basket;

    // get the maximum allowed windows space
    int x = area.left();
    int y = area.top();
    x_optimal = x;
    y_optimal = y;

    // client gabarit
    int ch = window->geo.update.frame.size().height() - 1;
    int cw = window->geo.update.frame.size().width() - 1;

    // CT lame flag. Don't like it. What else would do?
    bool first_pass = true;

    // loop over possible positions
    do {
        // test if enough room in x and y directions
        if (y + ch > area.bottom() && ch < area.height()) {
            // this throws the algorithm to an exit
            overlap = h_wrong;
        } else if (x + cw > area.right()) {
            overlap = w_wrong;
        } else {
            // initialize
            overlap = none;

            cxl = x;
            cxr = x + cw;
            cyt = y;
            cyb = y + ch;
            for (auto const& var_win : window->space.stacking.order.stack) {
                std::visit(overload{[&](auto&& win) {
                               if (is_irrelevant(win, window, desktop)) {
                                   return;
                               }
                               auto const& frame = win->geo.update.frame;
                               xl = frame.topLeft().x();
                               yt = frame.topLeft().y();
                               xr = xl + frame.size().width();
                               yb = yt + frame.size().height();

                               // if windows overlap, calc the overall overlapping
                               if ((cxl < xr) && (cxr > xl) && (cyt < yb) && (cyb > yt)) {
                                   xl = qMax(cxl, xl);
                                   xr = qMin(cxr, xr);
                                   yt = qMax(cyt, yt);
                                   yb = qMin(cyb, yb);
                                   if (win->control->keep_above) {
                                       overlap += 16 * (xr - xl) * (yb - yt);
                                   } else if (win->control->keep_below && !is_dock(win)) {
                                       // ignore KeepBelow windows
                                       // for placement (see X11Client::belongsToLayer() for Dock)
                                       overlap += 0;
                                   } else {
                                       overlap += (xr - xl) * (yb - yt);
                                   }
                               }
                           }},
                           var_win);
            }
        }

        // CT first time we get no overlap we stop.
        if (overlap == none) {
            x_optimal = x;
            y_optimal = y;
            break;
        }

        if (first_pass) {
            first_pass = false;
            min_overlap = overlap;
        } else if (overlap >= none && overlap < min_overlap) {
            // CT save the best position and the minimum overlap up to now
            min_overlap = overlap;
            x_optimal = x;
            y_optimal = y;
        }

        // really need to loop? test if there's any overlap
        if (overlap > none) {
            possible = area.right();

            if (possible - cw > x) {
                possible -= cw;
            }

            // compare to the position of each window on the same desk
            for (auto const& var_win : window->space.stacking.order.stack) {
                std::visit(overload{[&](auto&& win) {
                               if (is_irrelevant(win, window, desktop)) {
                                   return;
                               }

                               auto const& frame = win->geo.update.frame;
                               xl = frame.topLeft().x();
                               yt = frame.topLeft().y();
                               xr = xl + frame.size().width();
                               yb = yt + frame.size().height();

                               // if not enough room above or under the current tested window
                               // determine the first non-overlapped x position
                               if ((y < yb) && (yt < ch + y)) {
                                   if ((xr > x) && (possible > xr)) {
                                       possible = xr;
                                   }

                                   basket = xl - cw;
                                   if ((basket > x) && (possible > basket)) {
                                       possible = basket;
                                   }
                               }
                           }},
                           var_win);
            }
            x = possible;
        } else if (overlap == w_wrong) {
            // Not enough x dimension (overlap was wrong on horizontal)
            x = area.left();
            possible = area.bottom();

            if (possible - ch > y) {
                possible -= ch;
            }

            // test the position of each window on the desk
            for (auto const& var_win : window->space.stacking.order.stack) {
                std::visit(overload{[&](auto&& win) {
                               if (is_irrelevant(win, window, desktop)) {
                                   return;
                               }

                               auto const& frame = win->geo.update.frame;
                               xl = frame.topLeft().x();
                               yt = frame.topLeft().y();
                               xr = xl + frame.size().width();
                               yb = yt + frame.size().height();

                               // if not enough room to the left or right of the current tested
                               // window determine the first non-overlapped y position
                               if ((yb > y) && (possible > yb)) {
                                   possible = yb;
                               }

                               basket = yt - ch;
                               if ((basket > y) && (possible > basket)) {
                                   possible = basket;
                               }
                           }},
                           var_win);
            }

            y = possible;
        }
    } while ((overlap != none) && (overlap != h_wrong) && (y < area.bottom()));

    if (ch >= area.height()) {
        y_optimal = area.top();
    }

    // place the window
    move(window, QPoint(x_optimal, y_optimal));
}

/**
 * Place windows centered, on top of all others
 */
template<typename Win>
void place_centered(Win* window, QRect const& area)
{
    assert(area.isValid());

    int const xp = area.left() + (area.width() - window->geo.update.frame.size().width()) / 2;
    int const yp = area.top() + (area.height() - window->geo.update.frame.size().height()) / 2;

    // place the window
    move(window, QPoint(xp, yp));
}

/**
 * Place windows in the (0,0) corner, on top of all others
 */
template<typename Win>
void place_zero_cornered(Win* window, QRect const& area)
{
    assert(area.isValid());

    // get the maximum allowed windows space and desk's origin
    move(window, area.topLeft());
}

template<typename Win>
void place_utility(Win* window, QRect const& area)
{
    // TODO kwin should try to place utility windows next to their mainwindow, preferably at the
    // right edge, and going down if there are more of them if there's not enough place outside the
    // mainwindow, it should prefer top-right corner. use the default placement for now.
    place_with_policy(window, area, placement::global_default);
}

template<typename Win>
void place_on_screen_display(Win* window, QRect const& area)
{
    assert(area.isValid());

    // place at lower area of the screen
    int const x = area.left() + (area.width() - window->geo.update.frame.size().width()) / 2;
    int const y = area.top() + 2 * area.height() / 3 - window->geo.update.frame.size().height() / 2;

    move(window, QPoint(x, y));
}

template<typename Win>
void place_under_mouse(Win* window, QRect const& area)
{
    assert(area.isValid());

    auto geo = window->geo.update.frame;
    geo.moveCenter(window->space.input->cursor->pos());

    move(window, geo.topLeft());
    keep_in_area(window, area, false);
}

template<typename Win>
void place_maximizing(Win* window, QRect const& area)
{
    assert(area.isValid());

    if (window->isMaximizable() && window->maxSize().width() >= area.width()
        && window->maxSize().height() >= area.height()) {
        if (space_window_area(window->space, MaximizeArea, window) == area) {
            maximize(window, maximize_mode::full);
        } else {
            // If the geometry doesn't match default maximize area (xinerama case?), it's probably
            // better to use the given area
            window->setFrameGeometry(area);
        }
    } else {
        constrained_resize(window, window->maxSize().boundedTo(area.size()));
        place_with_policy(window, area, placement::smart);
    }
}

template<typename Win>
void place_on_main_window(Win* window, QRect const& area)
{
    assert(area.isValid());

    Win* place_on{nullptr};
    Win* place_on2{nullptr};

    int mains_count = 0;
    auto leads = window->transient->leads();

    for (auto lead : leads) {
        if (leads.size() > 1 && is_special_window(lead)) {
            // don't consider toolbars etc when placing
            continue;
        }

        ++mains_count;
        place_on2 = lead;

        if (on_current_desktop(lead)) {
            if (place_on == nullptr) {
                place_on = lead;
            } else {
                // Two or more on current desktop -> center. That's the default at least.
                place_with_policy(window, area, placement::centered);
                return;
            }
        }
    }

    if (!place_on) {
        // 'mains_count' is used because it doesn't include ignored mainwindows
        if (mains_count != 1) {
            place_with_policy(window, area, placement::centered);
            return;
        }

        // use the only window filtered together with 'mains_count'
        place_on = place_on2;
    }

    if (is_desktop(place_on)) {
        place_with_policy(window, area, placement::centered);
        return;
    }

    auto geo = window->geo.update.frame;
    geo.moveCenter(place_on->geo.update.frame.center());
    move(window, geo.topLeft());

    // get area again, because the mainwindow may be on different xinerama screen
    auto const placementArea = space_window_area(window->space, PlacementArea, window);
    keep_in_area(window, placementArea, false);
}

template<typename Win>
void place_dialog(Win* window, QRect const& area)
{
    if (window->space.base.options->qobject->placement() == placement::maximizing) {
        // With maximizing placement policy as the default, the dialog should be either maximized or
        // made as large as its maximum size and then placed on the main window (centered).
        place_maximizing(window, area);
    }
    place_on_main_window(window, area);
}

template<typename Win>
void place_with_policy(Win* window, QRect const& area, placement policy)
{
    if (policy == placement::unknown) {
        policy = placement::global_default;
    }
    if (policy == placement::global_default) {
        policy = window->space.base.options->qobject->placement();
    }

    if (policy == placement::no_placement) {
        return;
    }

    switch (policy) {
    case placement::random:
        place_at_random(window, area);
        break;
    case placement::centered:
        place_centered(window, area);
        break;
    case placement::zero_cornered:
        place_zero_cornered(window, area);
        break;
    case placement::under_mouse:
        place_under_mouse(window, area);
        break;
    case placement::on_main_window:
        place_on_main_window(window, area);
        break;
    case placement::maximizing:
        place_maximizing(window, area);
        break;
    default:
        place_smart(window, area);
    }

    if (window->space.base.options->qobject->borderSnapZone()) {
        // snap to titlebar / snap to window borders on inner screen edges
        auto const geo = window->geo.update.frame;
        auto corner = geo.topLeft();
        auto const margins = frame_margins(window);
        auto const win_area = space_window_area(window->space, FullArea, window);

        if (!(window->maximizeMode() & maximize_mode::horizontal)) {
            if (geo.right() == win_area.right()) {
                corner.rx() += margins.right();
            }
            if (geo.left() == win_area.left()) {
                corner.rx() -= margins.left();
            }
        }

        if (!(window->maximizeMode() & maximize_mode::vertical)) {
            if (geo.bottom() == win_area.bottom()) {
                corner.ry() += margins.bottom();
            }
        }

        move(window, corner);
    }
}

template<typename Win>
placement get_placement_policy(Win const& window)
{
    if (auto policy = window.control->rules.checkPlacement(placement::global_default);
        policy != placement::global_default) {
        // Placement overriden by rule.
        return policy;
    }
    return window.space.base.options->qobject->placement();
}

/**
 * Places the client \a c according to the workspace's layout policy
 */
template<typename Win>
void place_in_area(Win* window, QRect const& area)
{
    auto policy = window->control->rules.checkPlacement(placement::global_default);

    if (policy != placement::global_default) {
        place_with_policy(window, area, policy);
        return;
    }

    if (is_utility(window)) {
        place_utility(window, area);
        return;
    }
    if (is_dialog(window)) {
        place_dialog(window, area);
        return;
    }
    if (is_splash(window)) {
        // Place on main window, if any exists, otherwise centered.
        place_on_main_window(window, area);
        return;
    }
    if (is_on_screen_display(window) || is_notification(window)
        || is_critical_notification(window)) {
        place_on_screen_display(window, area);
        return;
    }

    // TODO(romangg): Remove this special case only there for Wayland/Xwayland windows.
    if constexpr (requires(Win win) { win.surface; }) {
        if (window->transient->lead() && window->surface) {
            place_dialog(window, area);
            return;
        }
    }

    place_with_policy(window, area, window->space.base.options->qobject->placement());
}

/**
 * Unclutters the current desktop by smart-placing all windows again.
 */
template<typename Space>
void unclutter_desktop(Space& space)
{
    auto const& windows = space.windows;
    for (int i = windows.size() - 1; i >= 0; i--) {
        std::visit(overload{[&](auto&& win) {
                       if (!win->control || !on_current_desktop(win) || win->control->minimized
                           || on_all_desktops(win) || !win->isMovable()) {
                           return;
                       }
                       auto const placementArea = space_window_area(space, PlacementArea, win);
                       place_smart(win, placementArea);
                   }},
                   windows.at(i));
    }
}

}
