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

#include "placement.h"

#ifndef KCMRULES
#include "workspace.h"
#include "cursor.h"
#include "options.h"
#include "rules/rules.h"
#include "screens.h"

#include "win/geo.h"
#include "win/meta.h"
#include "win/move.h"
#include "win/net.h"
#include "win/transient.h"
#endif

#include <QRect>
#include <QTextStream>
#include <QTimer>

namespace KWin
{

#ifndef KCMRULES

KWIN_SINGLETON_FACTORY(Placement)

Placement::Placement(QObject*)
{
}

Placement::~Placement()
{
    s_self = nullptr;
}

/**
 * Places the client \a c according to the workspace's layout policy
 */
void Placement::place(Toplevel* window, const QRect &area)
{
    auto policy = window->control->rules().checkPlacement(global_default);
    if (policy != global_default) {
        place(window, area, policy);
        return;
    }

    if (win::is_utility(window)) {
        place_utility(window, area, options->placement());
    } else if (win::is_dialog(window)) {
        place_dialog(window, area, options->placement());
    } else if (win::is_splash(window)) {
        // on mainwindow, if any, otherwise centered
        place_on_main_window(window, area);
    } else if (win::is_on_screen_display(window) || win::is_notification(window)
               || win::is_critical_notification(window)) {
        place_on_screen_display(window, area);
    } else if (window->isTransient() && window->surface()) {
        place_dialog(window, area, options->placement());
    } else {
        place(window, area, options->placement());
    }
}

void Placement::place(Toplevel* window, const QRect &area, Policy policy, Policy nextPlacement)
{
    if (policy == unknown)
        policy = global_default;
    if (policy == global_default)
        policy = options->placement();
    if (policy == no_placement)
        return;
    else if (policy == random)
        place_at_random(window, area, nextPlacement);
    else if (policy == centered)
        place_centered(window, area, nextPlacement);
    else if (policy == zero_cornered)
        place_zero_cornered(window, area, nextPlacement);
    else if (policy == under_mouse)
        place_under_mouse(window, area, nextPlacement);
    else if (policy == on_main_window)
        place_on_main_window(window, area, nextPlacement);
    else if (policy == maximizing)
        place_maximizing(window, area, nextPlacement);
    else
        place_smart(window, area, nextPlacement);

    if (options->borderSnapZone()) {
        // snap to titlebar / snap to window borders on inner screen edges
        auto const geo = window->geometry_update.frame;
        QPoint corner = geo.topLeft();
        auto const frameMargins = win::frame_margins(window);

        const QRect fullRect = workspace()->clientArea(FullArea, window);
        if (!win::flags(window->maximizeMode() & win::maximize_mode::horizontal)) {
            if (geo.right() == fullRect.right()) {
                corner.rx() += frameMargins.right();
            }
            if (geo.left() == fullRect.left()) {
                corner.rx() -= frameMargins.left();
            }
        }
        if (!win::flags(window->maximizeMode() & win::maximize_mode::vertical)) {
            if (geo.bottom() == fullRect.bottom()) {
                corner.ry() += frameMargins.bottom();
            }
        }
        win::move(window, corner);
    }
}

/**
 * Place the client \a c according to a simply "random" placement algorithm.
 */
void Placement::place_at_random(Toplevel* window, const QRect& area, Policy /*next*/)
{
    Q_ASSERT(area.isValid());

    const int step  = 24;
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
    win::move(window, QPoint(tx, ty));
}

// TODO: one day, there'll be C++11 ...
bool Placement::is_irrelevant(Toplevel const* window, Toplevel const* regarding, int desktop)
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
    if (!window->isOnCurrentActivity()) {
        return true;
    }
    if (win::is_desktop(window)) {
        return true;
    }
    return false;
}

/**
 * Place the client \a c according to a really smart placement algorithm :-)
 */
void Placement::place_smart(Toplevel* window, const QRect& area, Policy /*next*/)
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
    int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();

    int cxl, cxr, cyt, cyb;     //temp coords
    int  xl, xr, yt, yb;     //temp coords
    int basket;                 //temp holder

    // get the maximum allowed windows space
    int x = area.left();
    int y = area.top();
    x_optimal = x; y_optimal = y;

    //client gabarit
    int ch = window->geometry_update.frame.size().height() - 1;
    int cw = window->geometry_update.frame.size().width()  - 1;

    bool first_pass = true; //CT lame flag. Don't like it. What else would do?

    //loop over possible positions
    do {
        //test if enough room in x and y directions
        if (y + ch > area.bottom() && ch < area.height()) {
            overlap = h_wrong; // this throws the algorithm to an exit
        } else if (x + cw > area.right()) {
            overlap = w_wrong;
        } else {
            overlap = none; //initialize

            cxl = x; cxr = x + cw;
            cyt = y; cyb = y + ch;
            for (auto const& client : workspace()->stackingOrder()) {
                if (is_irrelevant(client, window, desktop)) {
                    continue;
                }
                xl = client->geometry_update.frame.topLeft().x();
                yt = client->geometry_update.frame.topLeft().y();
                xr = xl + client->geometry_update.frame.size().width();
                yb = yt + client->geometry_update.frame.size().height();

                //if windows overlap, calc the overall overlapping
                if ((cxl < xr) && (cxr > xl) &&
                        (cyt < yb) && (cyb > yt)) {
                    xl = qMax(cxl, xl); xr = qMin(cxr, xr);
                    yt = qMax(cyt, yt); yb = qMin(cyb, yb);
                    if (client->control->keep_above()) {
                        overlap += 16 * (xr - xl) * (yb - yt);
                    } else if (client->control->keep_below() && !win::is_dock(client)) {
                         // ignore KeepBelow windows
                        overlap += 0; // for placement (see X11Client::belongsToLayer() for Dock)
                    } else {
                        overlap += (xr - xl) * (yb - yt);
                    }
                }
            }
        }

        //CT first time we get no overlap we stop.
        if (overlap == none) {
            x_optimal = x;
            y_optimal = y;
            break;
        }

        if (first_pass) {
            first_pass = false;
            min_overlap = overlap;
        }
        //CT save the best position and the minimum overlap up to now
        else if (overlap >= none && overlap < min_overlap) {
            min_overlap = overlap;
            x_optimal = x;
            y_optimal = y;
        }

        // really need to loop? test if there's any overlap
        if (overlap > none) {

            possible = area.right();
            if (possible - cw > x) possible -= cw;

            // compare to the position of each client on the same desk
            for (auto const& client : workspace()->stackingOrder()) {
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

                    if ((xr > x) && (possible > xr)) possible = xr;

                    basket = xl - cw;
                    if ((basket > x) && (possible > basket)) possible = basket;
                }
            }
            x = possible;
        }

        // ... else ==> not enough x dimension (overlap was wrong on horizontal)
        else if (overlap == w_wrong) {
            x = area.left();
            possible = area.bottom();

            if (possible - ch > y) possible -= ch;

            //test the position of each window on the desk
            for (auto const& client : workspace()->stackingOrder()) {
                if (is_irrelevant(client, window, desktop)) {
                    continue;
                }

                xl = client->geometry_update.frame.topLeft().x();
                yt = client->geometry_update.frame.topLeft().y();
                xr = xl + client->geometry_update.frame.size().width();
                yb = yt + client->geometry_update.frame.size().height();

                // if not enough room to the left or right of the current tested client
                // determine the first non-overlapped y position
                if ((yb > y) && (possible > yb)) possible = yb;

                basket = yt - ch;
                if ((basket > y) && (possible > basket)) possible = basket;
            }
            y = possible;
        }
    } while ((overlap != none) && (overlap != h_wrong) && (y < area.bottom()));

    if (ch >= area.height()) {
        y_optimal = area.top();
    }

    // place the window
    win::move(window, QPoint(x_optimal, y_optimal));

}

/**
 * Place windows centered, on top of all others
 */
void Placement::place_centered(Toplevel* window, const QRect& area, Policy /*next*/)
{
    Q_ASSERT(area.isValid());

    const int xp = area.left() + (area.width() - window->geometry_update.frame.size().width()) / 2;
    const int yp = area.top() + (area.height() - window->geometry_update.frame.size().height()) / 2;

    // place the window
    win::move(window, QPoint(xp, yp));
}

/**
 * Place windows in the (0,0) corner, on top of all others
 */
void Placement::place_zero_cornered(Toplevel* window, const QRect& area, Policy /*next*/)
{
    Q_ASSERT(area.isValid());

    // get the maximum allowed windows space and desk's origin
    win::move(window, area.topLeft());
}

void Placement::place_utility(Toplevel* window, const QRect& area, Policy /*next*/)
{
// TODO kwin should try to place utility windows next to their mainwindow,
// preferably at the right edge, and going down if there are more of them
// if there's not enough place outside the mainwindow, it should prefer
// top-right corner
    // use the default placement for now
    place(window, area, global_default);
}

void Placement::place_on_screen_display(Toplevel* window, const QRect& area)
{
    Q_ASSERT(area.isValid());

    // place at lower area of the screen
    const int x = area.left() + (area.width() -  window->geometry_update.frame.size().width())  / 2;
    const int y = area.top() + 2 * area.height() / 3 - window->geometry_update.frame.size().height() / 2;

    win::move(window, QPoint(x, y));
}

void Placement::place_dialog(Toplevel* window, const QRect& area, Policy nextPlacement)
{
    place_on_main_window(window, area, nextPlacement);
}

void Placement::place_under_mouse(Toplevel* window, const QRect& area, Policy /*next*/)
{
    Q_ASSERT(area.isValid());

    auto geom = window->geometry_update.frame;
    geom.moveCenter(Cursor::pos());
    win::move(window, geom.topLeft());
    win::keep_in_area(window, area, false);   // make sure it's kept inside workarea
}

void Placement::place_on_main_window(Toplevel* window, const QRect& area, Policy nextPlacement)
{
    Q_ASSERT(area.isValid());

    if (nextPlacement == unknown)
        nextPlacement = centered;
    if (nextPlacement == maximizing) // maximize if needed
        place_maximizing(window, area, no_placement);

    auto leads = window->transient()->leads();
    Toplevel* place_on = nullptr;
    Toplevel* place_on2 = nullptr;
    int mains_count = 0;

    for (auto lead : leads) {
        if (leads.size() > 1 && win::is_special_window(lead)) {
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
                place(window, area, centered);
                return;
            }
        }
    }

    if (place_on == nullptr) {
        // 'mains_count' is used because it doesn't include ignored mainwindows
        if (mains_count != 1) {
            place(window, area, centered);
            return;
        }
        place_on = place_on2; // use the only window filtered together with 'mains_count'
    }
    if (win::is_desktop(place_on)) {
        place(window, area, centered);
        return;
    }
    auto geom = window->geometry_update.frame;
    geom.moveCenter(place_on->geometry_update.frame.center());
    win::move(window, geom.topLeft());
    // get area again, because the mainwindow may be on different xinerama screen
    const QRect placementArea = workspace()->clientArea(PlacementArea, window);
    win::keep_in_area(window, placementArea, false);   // make sure it's kept inside workarea
}

void Placement::place_maximizing(Toplevel* window, const QRect& area, Policy nextPlacement)
{
    Q_ASSERT(area.isValid());

    if (nextPlacement == unknown)
        nextPlacement = smart;
    if (window->isMaximizable() && window->maxSize().width() >= area.width()
        && window->maxSize().height() >= area.height()) {
        if (workspace()->clientArea(MaximizeArea, window) == area)
            win::maximize(window, win::maximize_mode::full);
        else { // if the geometry doesn't match default maximize area (xinerama case?),
            // it's probably better to use the given area
            window->setFrameGeometry(area);
        }
    } else {
        win::constrained_resize(window, window->maxSize().boundedTo(area.size()));
        place(window, area, nextPlacement);
    }
}

void Placement::unclutter_desktop()
{
    const auto &clients = Workspace::self()->allClientList();
    for (int i = clients.size() - 1; i >= 0; i--) {
        auto client = clients.at(i);
        if ((!client->isOnCurrentDesktop()) ||
                (client->control->minimized())     ||
                (client->isOnAllDesktops()) ||
                (!client->isMovable()))
            continue;
        const QRect placementArea = workspace()->clientArea(PlacementArea, client);
        place_smart(client, placementArea);
    }
}

bool Placement::can_move(Toplevel const* window)
{
    if (!window) {
        return false;
    }
    return window->isMovable();
}

#endif

const char* Placement::policy_to_string(Policy policy)
{
    const char* const policies[] = {
        "NoPlacement", "Default", "XXX should never see", "Random", "Smart", "Centered",
        "ZeroCornered", "UnderMouse", "OnMainWindow", "Maximizing"
    };
    Q_ASSERT(policy < int(sizeof(policies) / sizeof(policies[0])));
    return policies[policy];
}

} // namespace
