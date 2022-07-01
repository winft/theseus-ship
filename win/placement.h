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

#ifndef KCMRULES
#include "base/options.h"
#include "input/cursor.h"
#include "rules/rules.h"

#include "geo.h"
#include "kwinglobals.h"
#include "meta.h"
#include "move.h"
#include "net.h"
#include "space.h"
#include "stacking_order.h"
#include "transient.h"

#include <QList>
#include <QPoint>
#include <QRect>

namespace KWin
{
class Toplevel;

namespace win
{

template<typename Win>
static inline bool can_move(Win const* window);
template<typename Win1, typename Win2>
static inline bool is_irrelevant(Win1 const* window, Win2 const* regarding, int desktop);

template<typename Win>
void place(Win* window, const QRect& area);
template<typename Win>
void place(Win* window,
           const QRect& area,
           placement policy,
           placement nextPlacement = placement::unknown);

template<typename Win>
void place_at_random(Win* window, const QRect& area, placement next = placement::unknown);
template<typename Win>
void place_smart(Win* window, const QRect& area, placement next = placement::unknown);
template<typename Win>
void place_centered(Win* window, const QRect& area, placement next = placement::unknown);
template<typename Win>
void place_zero_cornered(Win* window, const QRect& area, placement next = placement::unknown);
template<typename Win>
void place_under_mouse(Win* window, const QRect& area, placement next = placement::unknown);
template<typename Win>
void place_on_main_window(Win* window, const QRect& area, placement next = placement::unknown);
template<typename Win>
void place_maximizing(Win* window, const QRect& area, placement next = placement::unknown);

template<typename Win>
void place_on_screen_display(Win* window, const QRect& area);
template<typename Win>
void place_dialog(Win* window, const QRect& area, placement next = placement::unknown);
template<typename Win>
void place_utility(Win* window, const QRect& area, placement next = placement::unknown);

void unclutter_desktop();

// ********************
// definitions
// ********************

template<typename Win>
bool can_move(Win const* window)
{
    if (!window) {
        return false;
    }
    return window->isMovable();
}

template<typename Win1, typename Win2>
static inline bool is_irrelevant(Win1 const* window, Win2 const* regarding, int desktop)
{
    if (!window) {
        return true;
    }
    if (!window->control) {
        return true;
    }
    if (window == regarding) {
        return true;
    }
    if (!window->isShown()) {
        return true;
    }
    if (!window->isOnDesktop(desktop)) {
        return true;
    }
    if (is_desktop(window)) {
        return true;
    }
    return false;
}

/**
 * Places the client \a c according to the workspace's layout policy
 */
template<typename Win>
void place(Win* window, const QRect& area)
{
    auto policy = window->control->rules().checkPlacement(placement::global_default);
    if (policy != placement::global_default) {
        place(window, area, policy);
        return;
    }

    if (is_utility(window)) {
        place_utility(window, area, kwinApp()->options->placement());
    } else if (is_dialog(window)) {
        place_dialog(window, area, kwinApp()->options->placement());
    } else if (is_splash(window)) {
        // on mainwindow, if any, otherwise centered
        place_on_main_window(window, area);
    } else if (is_on_screen_display(window) || is_notification(window)
               || is_critical_notification(window)) {
        place_on_screen_display(window, area);
    } else if (window->transient()->lead() && window->surface) {
        place_dialog(window, area, kwinApp()->options->placement());
    } else {
        place(window, area, kwinApp()->options->placement());
    }
}

template<typename Win>
void place(Win* window, const QRect& area, placement policy, placement nextPlacement)
{
    switch (policy) {
    case placement::unknown:
        policy = kwinApp()->options->placement();
        [[fallthrough]];
    case placement::global_default:
        policy = kwinApp()->options->placement();
        [[fallthrough]];
    case placement::no_placement:
        return;
    case placement::random:
        place_at_random(window, area, nextPlacement);
        break;
    case placement::centered:
        place_centered(window, area, nextPlacement);
        break;
    case placement::zero_cornered:
        place_zero_cornered(window, area, nextPlacement);
        break;
    case placement::under_mouse:
        place_under_mouse(window, area, nextPlacement);
        break;
    case placement::on_main_window:
        place_on_main_window(window, area, nextPlacement);
        break;
    case placement::maximizing:
        place_maximizing(window, area, nextPlacement);
        break;
    default:
        place_smart(window, area, nextPlacement);
    }

    if (kwinApp()->options->borderSnapZone()) {
        // snap to titlebar / snap to window borders on inner screen edges
        auto const geo = window->geometry_update.frame;
        QPoint corner = geo.topLeft();
        auto const frameMargins = frame_margins(window);

        const QRect fullRect = window->space.clientArea(FullArea, window);
        if (!(window->maximizeMode() & maximize_mode::horizontal)) {
            if (geo.right() == fullRect.right()) {
                corner.rx() += frameMargins.right();
            }
            if (geo.left() == fullRect.left()) {
                corner.rx() -= frameMargins.left();
            }
        }
        if (!(window->maximizeMode() & maximize_mode::vertical)) {
            if (geo.bottom() == fullRect.bottom()) {
                corner.ry() += frameMargins.bottom();
            }
        }
        move(window, corner);
    }
}

/**
 * Place the client \a c according to a simply "random" placement algorithm.
 */
template<typename Win>
void place_at_random(Win* window, const QRect& area, placement /*next*/)
{
    Q_ASSERT(area.isValid());

    const int step = 24;
    static int px = step;
    static int py = 2 * step;
    int tx, ty;

    if (px < area.x()) {
        px = area.x();
    }
    if (py < area.y()) {
        py = area.y();
    }

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
    if (tx + window->geometry_update.frame.size().width() > area.right()) {
        tx = area.right() - window->geometry_update.frame.size().width();
        if (tx < 0)
            tx = 0;
        px = area.x();
    }
    if (ty + window->geometry_update.frame.size().height() > area.bottom()) {
        ty = area.bottom() - window->geometry_update.frame.size().height();
        if (ty < 0)
            ty = 0;
        py = area.y();
    }
    move(window, QPoint(tx, ty));
}

/**
 * Place the client \a c according to a really smart placement algorithm :-)
 */
template<typename Win>
void place_smart(Win* window, const QRect& area, placement /*next*/)
{
    Q_ASSERT(area.isValid());

    /*
     * SmartPlacement by Cristian Tibirna (tibirna@kde.org)
     * adapted for kwm (16-19jan98) and for kwin (16Nov1999) using (with
     * permission) ideas from fvwm, authored by
     * Anthony Martin (amartin@engr.csulb.edu).
     * Xinerama supported added by Balaji Ramani (balaji@yablibli.com)
     * with ideas from xfce.
     */

    if (!window->geometry_update.frame.size().isValid()) {
        return;
    }

    const int none = 0, h_wrong = -1, w_wrong = -2; // overlap types
    long int overlap, min_overlap = 0;
    int x_optimal, y_optimal;
    int possible;
    int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? window->space.virtual_desktop_manager->current()
        : window->desktop();

    int cxl, cxr, cyt, cyb; // temp coords
    int xl, xr, yt, yb;     // temp coords
    int basket;             // temp holder

    // get the maximum allowed windows space
    int x = area.left();
    int y = area.top();
    x_optimal = x;
    y_optimal = y;

    // client gabarit
    int ch = window->geometry_update.frame.size().height() - 1;
    int cw = window->geometry_update.frame.size().width() - 1;

    bool first_pass = true; // CT lame flag. Don't like it. What else would do?

    // loop over possible positions
    do {
        // test if enough room in x and y directions
        if (y + ch > area.bottom() && ch < area.height()) {
            overlap = h_wrong; // this throws the algorithm to an exit
        } else if (x + cw > area.right()) {
            overlap = w_wrong;
        } else {
            overlap = none; // initialize

            cxl = x;
            cxr = x + cw;
            cyt = y;
            cyb = y + ch;
            for (auto const& client : window->space.stacking_order->stack) {
                if (is_irrelevant(client, window, desktop)) {
                    continue;
                }
                xl = client->geometry_update.frame.topLeft().x();
                yt = client->geometry_update.frame.topLeft().y();
                xr = xl + client->geometry_update.frame.size().width();
                yb = yt + client->geometry_update.frame.size().height();

                // if windows overlap, calc the overall overlapping
                if ((cxl < xr) && (cxr > xl) && (cyt < yb) && (cyb > yt)) {
                    xl = qMax(cxl, xl);
                    xr = qMin(cxr, xr);
                    yt = qMax(cyt, yt);
                    yb = qMin(cyb, yb);
                    if (client->control->keep_above()) {
                        overlap += 16 * (xr - xl) * (yb - yt);
                    } else if (client->control->keep_below() && !is_dock(client)) {
                        // ignore KeepBelow windows
                        overlap += 0; // for placement (see X11Client::belongsToLayer() for Dock)
                    } else {
                        overlap += (xr - xl) * (yb - yt);
                    }
                }
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
        }
        // CT save the best position and the minimum overlap up to now
        else if (overlap >= none && overlap < min_overlap) {
            min_overlap = overlap;
            x_optimal = x;
            y_optimal = y;
        }

        // really need to loop? test if there's any overlap
        if (overlap > none) {

            possible = area.right();
            if (possible - cw > x)
                possible -= cw;

            // compare to the position of each client on the same desk
            for (auto const& client : window->space.stacking_order->stack) {
                if (is_irrelevant(client, window, desktop)) {
                    continue;
                }

                xl = client->geometry_update.frame.topLeft().x();
                yt = client->geometry_update.frame.topLeft().y();
                xr = xl + client->geometry_update.frame.size().width();
                yb = yt + client->geometry_update.frame.size().height();

                // if not enough room above or under the current tested client
                // determine the first non-overlapped x position
                if ((y < yb) && (yt < ch + y)) {

                    if ((xr > x) && (possible > xr))
                        possible = xr;

                    basket = xl - cw;
                    if ((basket > x) && (possible > basket))
                        possible = basket;
                }
            }
            x = possible;
        }

        // ... else ==> not enough x dimension (overlap was wrong on horizontal)
        else if (overlap == w_wrong) {
            x = area.left();
            possible = area.bottom();

            if (possible - ch > y)
                possible -= ch;

            // test the position of each window on the desk
            for (auto const& client : window->space.stacking_order->stack) {
                if (is_irrelevant(client, window, desktop)) {
                    continue;
                }

                xl = client->geometry_update.frame.topLeft().x();
                yt = client->geometry_update.frame.topLeft().y();
                xr = xl + client->geometry_update.frame.size().width();
                yb = yt + client->geometry_update.frame.size().height();

                // if not enough room to the left or right of the current tested client
                // determine the first non-overlapped y position
                if ((yb > y) && (possible > yb))
                    possible = yb;

                basket = yt - ch;
                if ((basket > y) && (possible > basket))
                    possible = basket;
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
void place_centered(Win* window, const QRect& area, placement /*next*/)
{
    Q_ASSERT(area.isValid());

    const int xp = area.left() + (area.width() - window->geometry_update.frame.size().width()) / 2;
    const int yp = area.top() + (area.height() - window->geometry_update.frame.size().height()) / 2;

    // place the window
    move(window, QPoint(xp, yp));
}

/**
 * Place windows in the (0,0) corner, on top of all others
 */
template<typename Win>
void place_zero_cornered(Win* window, const QRect& area, placement /*next*/)
{
    Q_ASSERT(area.isValid());

    // get the maximum allowed windows space and desk's origin
    move(window, area.topLeft());
}

template<typename Win>
void place_utility(Win* window, const QRect& area, placement /*next*/)
{
    // TODO kwin should try to place utility windows next to their mainwindow,
    // preferably at the right edge, and going down if there are more of them
    // if there's not enough place outside the mainwindow, it should prefer
    // top-right corner
    // use the default placement for now
    place(window, area, placement::global_default);
}

template<typename Win>
void place_on_screen_display(Win* window, const QRect& area)
{
    Q_ASSERT(area.isValid());

    // place at lower area of the screen
    const int x = area.left() + (area.width() - window->geometry_update.frame.size().width()) / 2;
    const int y
        = area.top() + 2 * area.height() / 3 - window->geometry_update.frame.size().height() / 2;

    move(window, QPoint(x, y));
}

template<typename Win>
void place_dialog(Win* window, const QRect& area, placement nextPlacement)
{
    place_on_main_window(window, area, nextPlacement);
}

template<typename Win>
void place_under_mouse(Win* window, const QRect& area, placement /*next*/)
{
    Q_ASSERT(area.isValid());

    auto geom = window->geometry_update.frame;
    geom.moveCenter(input::get_cursor()->pos());
    move(window, geom.topLeft());
    keep_in_area(window, area, false); // make sure it's kept inside workarea
}

template<typename Win>
void place_on_main_window(Win* window, const QRect& area, placement nextPlacement)
{
    Q_ASSERT(area.isValid());

    if (nextPlacement == placement::unknown)
        nextPlacement = placement::centered;
    if (nextPlacement == placement::maximizing) // maximize if needed
        place_maximizing(window, area, placement::no_placement);

    auto leads = window->transient()->leads();
    Toplevel* place_on = nullptr;
    Toplevel* place_on2 = nullptr;
    int mains_count = 0;

    for (auto lead : leads) {
        if (leads.size() > 1 && is_special_window(lead)) {
            // don't consider toolbars etc when placing
            continue;
        }

        ++mains_count;
        place_on2 = lead;

        if (lead->isOnCurrentDesktop()) {
            if (place_on == nullptr) {
                place_on = lead;
            } else {
                // two or more on current desktop -> center
                // That's the default at least. However, with maximizing placement
                // policy as the default, the dialog should be either maximized or
                // made as large as its maximum size and then placed centered.
                // So the nextPlacement argument allows chaining. In this case, nextPlacement
                // is maximizing and it will call place_centered().
                place(window, area, placement::centered);
                return;
            }
        }
    }

    if (place_on == nullptr) {
        // 'mains_count' is used because it doesn't include ignored mainwindows
        if (mains_count != 1) {
            place(window, area, placement::centered);
            return;
        }
        place_on = place_on2; // use the only window filtered together with 'mains_count'
    }
    if (is_desktop(place_on)) {
        place(window, area, placement::centered);
        return;
    }
    auto geom = window->geometry_update.frame;
    geom.moveCenter(place_on->geometry_update.frame.center());
    move(window, geom.topLeft());
    // get area again, because the mainwindow may be on different xinerama screen
    const QRect placementArea = window->space.clientArea(PlacementArea, window);
    keep_in_area(window, placementArea, false); // make sure it's kept inside workarea
}

template<typename Win>
void place_maximizing(Win* window, const QRect& area, placement nextPlacement)
{
    Q_ASSERT(area.isValid());

    if (nextPlacement == placement::unknown)
        nextPlacement = placement::smart;
    if (window->isMaximizable() && window->maxSize().width() >= area.width()
        && window->maxSize().height() >= area.height()) {
        if (window->space.clientArea(MaximizeArea, window) == area)
            maximize(window, maximize_mode::full);
        else { // if the geometry doesn't match default maximize area (xinerama case?),
            // it's probably better to use the given area
            window->setFrameGeometry(area);
        }
    } else {
        constrained_resize(window, window->maxSize().boundedTo(area.size()));
        place(window, area, nextPlacement);
    }
}

/**
 * Unclutters the current desktop by smart-placing all windows again.
 */
inline void unclutter_desktop(win::space& space)
{
    auto const& windows = space.m_windows;
    for (int i = windows.size() - 1; i >= 0; i--) {
        auto client = windows.at(i);
        if (!client->control || !client->isOnCurrentDesktop() || client->control->minimized()
            || client->isOnAllDesktops() || !client->isMovable()) {
            continue;
        }
        const QRect placementArea = space.clientArea(PlacementArea, client);
        place_smart(client, placementArea);
    }
}

}
}

#endif
