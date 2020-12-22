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
    reinitCascading(0);
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
    auto policy = window->control->rules().checkPlacement(Default);
    if (policy != Default) {
        place(window, area, policy);
        return;
    }

    if (win::is_utility(window)) {
        placeUtility(window, area, options->placement());
    } else if (win::is_dialog(window)) {
        placeDialog(window, area, options->placement());
    } else if (win::is_splash(window)) {
        // on mainwindow, if any, otherwise centered
        placeOnMainWindow(window, area);
    } else if (win::is_on_screen_display(window) || win::is_notification(window)
        || win::is_critical_notification(window)) {
        placeOnScreenDisplay(window, area);
    } else if (window->isTransient() && window->surface()) {
        placeDialog(window, area, options->placement());
    } else {
        place(window, area, options->placement());
    }
}

void Placement::place(Toplevel* window, const QRect &area, Policy policy, Policy nextPlacement)
{
    if (policy == Unknown)
        policy = Default;
    if (policy == Default)
        policy = options->placement();
    if (policy == NoPlacement)
        return;
    else if (policy == Random)
        placeAtRandom(window, area, nextPlacement);
    else if (policy == Cascade)
        placeCascaded(window, area, nextPlacement);
    else if (policy == Centered)
        placeCentered(window, area, nextPlacement);
    else if (policy == ZeroCornered)
        placeZeroCornered(window, area, nextPlacement);
    else if (policy == UnderMouse)
        placeUnderMouse(window, area, nextPlacement);
    else if (policy == OnMainWindow)
        placeOnMainWindow(window, area, nextPlacement);
    else if (policy == Maximizing)
        placeMaximizing(window, area, nextPlacement);
    else
        placeSmart(window, area, nextPlacement);

    if (options->borderSnapZone()) {
        // snap to titlebar / snap to window borders on inner screen edges
        const QRect geo(window->frameGeometry());
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
void Placement::placeAtRandom(Toplevel* window, const QRect& area, Policy /*next*/)
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
    if (tx + window->size().width() > area.right()) {
        tx = area.right() - window->size().width();
        if (tx < 0)
            tx = 0;
        px = area.x();
    }
    if (ty + window->size().height() > area.bottom()) {
        ty = area.bottom() - window->size().height();
        if (ty < 0)
            ty = 0;
        py = area.y();
    }
    win::move(window, QPoint(tx, ty));
}

// TODO: one day, there'll be C++11 ...
static inline bool isIrrelevant(Toplevel const* window, Toplevel const* regarding, int desktop)
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
    if (!window->isShown(false)) {
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
void Placement::placeSmart(Toplevel* window, const QRect& area, Policy /*next*/)
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

    if (!window->size().isValid()) {
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
    int ch = window->size().height() - 1;
    int cw = window->size().width()  - 1;

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
                if (isIrrelevant(client, window, desktop)) {
                    continue;
                }
                xl = client->pos().x();
                yt = client->pos().y();
                xr = xl + client->size().width();
                yb = yt + client->size().height();

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
                if (isIrrelevant(client, window, desktop)) {
                    continue;
                }

                xl = client->pos().x();
                yt = client->pos().y();
                xr = xl + client->size().width();
                yb = yt + client->size().height();

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
                if (isIrrelevant(client, window, desktop)) {
                    continue;
                }

                xl = client->pos().x();
                yt = client->pos().y();
                xr = xl + client->size().width();
                yb = yt + client->size().height();

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

void Placement::reinitCascading(int desktop)
{
    // desktop == 0 - reinit all
    if (desktop == 0) {
        cci.clear();
        for (uint i = 0; i < VirtualDesktopManager::self()->count(); ++i) {
            DesktopCascadingInfo inf;
            inf.pos = QPoint(-1, -1);
            inf.col = 0;
            inf.row = 0;
            cci.append(inf);
        }
    } else {
        cci[desktop - 1].pos = QPoint(-1, -1);
        cci[desktop - 1].col = cci[desktop - 1].row = 0;
    }
}

QPoint Workspace::cascadeOffset(Toplevel const* window) const
{
    QRect area = clientArea(PlacementArea, window->frameGeometry().center(), window->desktop());
    return QPoint(area.width()/48, area.height()/48);
}

/**
 * Place windows in a cascading order, remembering positions for each desktop
 */
void Placement::placeCascaded(Toplevel* window, const QRect &area, Policy nextPlacement)
{
    Q_ASSERT(area.isValid());

    if (!window->size().isValid()) {
        return;
    }

    /* cascadePlacement by Cristian Tibirna (tibirna@kde.org) (30Jan98)
     */
    // work coords
    int xp, yp;

    //CT how do I get from the 'Client' class the size that NW squarish "handle"
    const QPoint delta = workspace()->cascadeOffset(window);

    const int dn = window->desktop() == 0 || window->isOnAllDesktops() ? (VirtualDesktopManager::self()->current() - 1) : (window->desktop() - 1);

    // initialize often used vars: width and height of c; we gain speed
    const int ch = window->size().height();
    const int cw = window->size().width();
    const int X = area.left();
    const int Y = area.top();
    const int H = area.height();
    const int W = area.width();

    if (nextPlacement == Unknown)
        nextPlacement = Smart;

    //initialize if needed
    if (cci[dn].pos.x() < 0 || cci[dn].pos.x() < X || cci[dn].pos.y() < Y) {
        cci[dn].pos = QPoint(X, Y);
        cci[dn].col = cci[dn].row = 0;
    }


    xp = cci[dn].pos.x();
    yp = cci[dn].pos.y();

    //here to touch in case people vote for resize on placement
    if ((yp + ch) > H) yp = Y;

    if ((xp + cw) > W) {
        if (!yp) {
            place(window, area, nextPlacement);
            return;
        } else xp = X;
    }

    //if this isn't the first window
    if (cci[dn].pos.x() != X && cci[dn].pos.y() != Y) {
        /* The following statements cause an internal compiler error with
         * egcs-2.91.66 on SuSE Linux 6.3. The equivalent forms compile fine.
         * 22-Dec-1999 CS
         *
         * if (xp != X && yp == Y) xp = delta.x() * (++(cci[dn].col));
         * if (yp != Y && xp == X) yp = delta.y() * (++(cci[dn].row));
         */
        if (xp != X && yp == Y) {
            ++(cci[dn].col);
            xp = delta.x() * cci[dn].col;
        }
        if (yp != Y && xp == X) {
            ++(cci[dn].row);
            yp = delta.y() * cci[dn].row;
        }

        // last resort: if still doesn't fit, smart place it
        if (((xp + cw) > W - X) || ((yp + ch) > H - Y)) {
            place(window, area, nextPlacement);
            return;
        }
    }

    // place the window
    win::move(window, QPoint(xp, yp));

    // new position
    cci[dn].pos = QPoint(xp + delta.x(), yp + delta.y());
}

/**
 * Place windows centered, on top of all others
 */
void Placement::placeCentered(Toplevel* window, const QRect& area, Policy /*next*/)
{
    Q_ASSERT(area.isValid());

    const int xp = area.left() + (area.width() - window->size().width()) / 2;
    const int yp = area.top() + (area.height() - window->size().height()) / 2;

    // place the window
    win::move(window, QPoint(xp, yp));
}

/**
 * Place windows in the (0,0) corner, on top of all others
 */
void Placement::placeZeroCornered(Toplevel* window, const QRect& area, Policy /*next*/)
{
    Q_ASSERT(area.isValid());

    // get the maximum allowed windows space and desk's origin
    win::move(window, area.topLeft());
}

void Placement::placeUtility(Toplevel* window, const QRect &area, Policy /*next*/)
{
// TODO kwin should try to place utility windows next to their mainwindow,
// preferably at the right edge, and going down if there are more of them
// if there's not enough place outside the mainwindow, it should prefer
// top-right corner
    // use the default placement for now
    place(window, area, Default);
}

void Placement::placeOnScreenDisplay(Toplevel* window, const QRect &area)
{
    Q_ASSERT(area.isValid());

    // place at lower area of the screen
    const int x = area.left() + (area.width() -  window->size().width())  / 2;
    const int y = area.top() + 2 * area.height() / 3 - window->size().height() / 2;

    win::move(window, QPoint(x, y));
}

void Placement::placeDialog(Toplevel* window, const QRect &area, Policy nextPlacement)
{
    placeOnMainWindow(window, area, nextPlacement);
}

void Placement::placeUnderMouse(Toplevel* window, const QRect &area, Policy /*next*/)
{
    Q_ASSERT(area.isValid());

    QRect geom = window->frameGeometry();
    geom.moveCenter(Cursor::pos());
    win::move(window, geom.topLeft());
    win::keep_in_area(window, area, false);   // make sure it's kept inside workarea
}

void Placement::placeOnMainWindow(Toplevel* window, const QRect &area, Policy nextPlacement)
{
    Q_ASSERT(area.isValid());

    if (nextPlacement == Unknown)
        nextPlacement = Centered;
    if (nextPlacement == Maximizing)   // maximize if needed
        placeMaximizing(window, area, NoPlacement);

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
                // is Maximizing and it will call placeCentered().
                place(window, area, Centered);
                return;
            }
        }
    }

    if (place_on == nullptr) {
        // 'mains_count' is used because it doesn't include ignored mainwindows
        if (mains_count != 1) {
            place(window, area, Centered);
            return;
        }
        place_on = place_on2; // use the only window filtered together with 'mains_count'
    }
    if (win::is_desktop(place_on)) {
        place(window, area, Centered);
        return;
    }
    QRect geom = window->frameGeometry();
    geom.moveCenter(place_on->frameGeometry().center());
    win::move(window, geom.topLeft());
    // get area again, because the mainwindow may be on different xinerama screen
    const QRect placementArea = workspace()->clientArea(PlacementArea, window);
    win::keep_in_area(window, placementArea, false);   // make sure it's kept inside workarea
}

void Placement::placeMaximizing(Toplevel* window, const QRect &area, Policy nextPlacement)
{
    Q_ASSERT(area.isValid());

    if (nextPlacement == Unknown)
        nextPlacement = Smart;
    if (window->isMaximizable() && window->maxSize().width() >= area.width() && window->maxSize().height() >= area.height()) {
        if (workspace()->clientArea(MaximizeArea, window) == area)
            win::maximize(window, win::maximize_mode::full);
        else { // if the geometry doesn't match default maximize area (xinerama case?),
            // it's probably better to use the given area
            window->setFrameGeometry(area);
        }
    } else {
        window->resizeWithChecks(window->maxSize().boundedTo(area.size()));
        place(window, area, nextPlacement);
    }
}

void Placement::cascadeDesktop()
{
    Workspace *ws = Workspace::self();
    const int desktop = VirtualDesktopManager::self()->current();
    reinitCascading(desktop);
    for (auto const& window : ws->stackingOrder()) {
        if (!window->control || !window->isOnCurrentDesktop() ||
                window->control->minimized() ||
                window->isOnAllDesktops() ||
                !window->isMovable()) {
            continue;
        }
        auto const placementArea = workspace()->clientArea(PlacementArea, window);
        placeCascaded(window, placementArea);
    }
}

void Placement::unclutterDesktop()
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
        placeSmart(client, placementArea);
    }
}

#endif


Placement::Policy Placement::policyFromString(const QString& policy, bool no_special)
{
    if (policy == QStringLiteral("NoPlacement"))
        return NoPlacement;
    else if (policy == QStringLiteral("Default") && !no_special)
        return Default;
    else if (policy == QStringLiteral("Random"))
        return Random;
    else if (policy == QStringLiteral("Cascade"))
        return Cascade;
    else if (policy == QStringLiteral("Centered"))
        return Centered;
    else if (policy == QStringLiteral("ZeroCornered"))
        return ZeroCornered;
    else if (policy == QStringLiteral("UnderMouse"))
        return UnderMouse;
    else if (policy == QStringLiteral("OnMainWindow") && !no_special)
        return OnMainWindow;
    else if (policy == QStringLiteral("Maximizing"))
        return Maximizing;
    else
        return Smart;
}

const char* Placement::policyToString(Policy policy)
{
    const char* const policies[] = {
        "NoPlacement", "Default", "XXX should never see", "Random", "Smart", "Cascade", "Centered",
        "ZeroCornered", "UnderMouse", "OnMainWindow", "Maximizing"
    };
    Q_ASSERT(policy < int(sizeof(policies) / sizeof(policies[ 0 ])));
    return policies[ policy ];
}


#ifndef KCMRULES

// ********************
// Workspace
// ********************

bool can_move(Toplevel* window)
{
    if (!window) {
        return false;
    }
    return window->isMovable();
}

/**
 * Moves active window left until in bumps into another window or workarea edge.
 */
void Workspace::slotWindowPackLeft()
{
    if (!can_move(active_client)) {
        return;
    }
    auto const pos = active_client->pos();
    win::pack_to(active_client, packPositionLeft(active_client, pos.x(), true), pos.y());
}

void Workspace::slotWindowPackRight()
{
    if (!can_move(active_client)) {
        return;
    }
    auto const pos = active_client->pos();
    auto const width = active_client->size().width();
    win::pack_to(active_client,
                 packPositionRight(active_client, pos.x() + width, true) - width + 1,
                 pos.y());
}

void Workspace::slotWindowPackUp()
{
    if (!can_move(active_client)) {
        return;
    }
    auto const pos = active_client->pos();
    win::pack_to(active_client, pos.x(), packPositionUp(active_client, pos.y(), true));
}

void Workspace::slotWindowPackDown()
{
    if (!can_move(active_client)) {
        return;
    }
    auto const pos = active_client->pos();
    auto const height = active_client->size().height();
    win::pack_to(active_client,
                 pos.x(),
                 packPositionDown(active_client, pos.y() + height, true) - height + 1);
}

void Workspace::slotWindowGrowHorizontal()
{
    if (active_client) {
        win::grow_horizontal(active_client);
    }
}

void Workspace::slotWindowShrinkHorizontal()
{
    if (active_client) {
        win::shrink_horizontal(active_client);
    }
}
void Workspace::slotWindowGrowVertical()
{
    if (active_client) {
        win::grow_vertical(active_client);
    }
}

void Workspace::slotWindowShrinkVertical()
{
    if (active_client) {
        win::shrink_vertical(active_client);
    }
}

void Workspace::quickTileWindow(win::quicktiles mode)
{
    if (!active_client) {
        return;
    }

    // If the user invokes two of these commands in a one second period, try to
    // combine them together to enable easy and intuitive corner tiling
    if (!m_quickTileCombineTimer->isActive()) {
        m_quickTileCombineTimer->start(1000);
        m_lastTilingMode = mode;
    } else {
        auto const was_left_or_right = m_lastTilingMode == win::quicktiles::left
            || m_lastTilingMode == win::quicktiles::right;
        auto const was_top_or_bottom = m_lastTilingMode == win::quicktiles::top
            || m_lastTilingMode == win::quicktiles::bottom;

        auto const is_left_or_right = mode == win::quicktiles::left
            || mode == win::quicktiles::right;
        auto const is_top_or_bottom = mode == win::quicktiles::top
            || mode == win::quicktiles::bottom;

        if ((was_left_or_right && is_top_or_bottom) || (was_top_or_bottom && is_left_or_right)) {
            mode |= m_lastTilingMode;
        }
        m_quickTileCombineTimer->stop();
    }

    win::set_quicktile_mode(active_client, mode, true);
}

int Workspace::packPositionLeft(Toplevel const* window, int oldX, bool leftEdge) const
{
    int newX = clientArea(MaximizeArea, window).left();
    if (oldX <= newX) { // try another Xinerama screen
        newX = clientArea(MaximizeArea,
                          QPoint(window->frameGeometry().left() - 1, window->frameGeometry().center().y()), window->desktop()).left();
    }

    auto const right = newX - win::frame_margins(window).left();
    QRect frameGeometry = window->frameGeometry();
    frameGeometry.moveRight(right);
    if (screens()->intersecting(frameGeometry) < 2) {
        newX = right;
    }

    if (oldX <= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (isIrrelevant(*it, window, desktop)) {
            continue;
        }
        const int x = leftEdge ? (*it)->frameGeometry().right() + 1 : (*it)->frameGeometry().left() - 1;
        if (x > newX && x < oldX
                && !(window->frameGeometry().top() > (*it)->frameGeometry().bottom()  // they overlap in Y direction
                     || window->frameGeometry().bottom() < (*it)->frameGeometry().top())) {
            newX = x;
        }
    }
    return newX;
}

int Workspace::packPositionRight(Toplevel const* window, int oldX, bool rightEdge) const
{
    int newX = clientArea(MaximizeArea, window).right();

    if (oldX >= newX) {
        // try another Xinerama screen
        newX = clientArea(MaximizeArea,
                          QPoint(window->frameGeometry().right() + 1, window->frameGeometry().center().y()), window->desktop()).right();
    }

    auto const right = newX + win::frame_margins(window).right();
    QRect frameGeometry = window->frameGeometry();
    frameGeometry.moveRight(right);
    if (screens()->intersecting(frameGeometry) < 2) {
        newX = right;
    }

    if (oldX >= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (isIrrelevant(*it, window, desktop)) {
            continue;
        }
        const int x = rightEdge ? (*it)->frameGeometry().left() - 1 : (*it)->frameGeometry().right() + 1;
        if (x < newX && x > oldX
                && !(window->frameGeometry().top() > (*it)->frameGeometry().bottom()
                     || window->frameGeometry().bottom() < (*it)->frameGeometry().top())) {
            newX = x;
        }
    }
    return newX;
}

int Workspace::packPositionUp(Toplevel const* window, int oldY, bool topEdge) const
{
    int newY = clientArea(MaximizeArea, window).top();
    if (oldY <= newY) { // try another Xinerama screen
        newY = clientArea(MaximizeArea,
                          QPoint(window->frameGeometry().center().x(), window->frameGeometry().top() - 1), window->desktop()).top();
    }

    if (oldY <= newY) {
        return oldY;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (isIrrelevant(*it, window, desktop)) {
            continue;
        }
        const int y = topEdge ? (*it)->frameGeometry().bottom() + 1 : (*it)->frameGeometry().top() - 1;
        if (y > newY && y < oldY
                && !(window->frameGeometry().left() > (*it)->frameGeometry().right()  // they overlap in X direction
                     || window->frameGeometry().right() < (*it)->frameGeometry().left())) {
            newY = y;
        }
    }
    return newY;
}

int Workspace::packPositionDown(Toplevel const* window, int oldY, bool bottomEdge) const
{
    int newY = clientArea(MaximizeArea, window).bottom();
    if (oldY >= newY) { // try another Xinerama screen
        newY = clientArea(MaximizeArea,
                          QPoint(window->frameGeometry().center().x(), window->frameGeometry().bottom() + 1), window->desktop()).bottom();
    }

    auto const bottom = newY + win::frame_margins(window).bottom();
    QRect frameGeometry = window->frameGeometry();
    frameGeometry.moveBottom(bottom);
    if (screens()->intersecting(frameGeometry) < 2) {
        newY = bottom;
    }

    if (oldY >= newY) {
        return oldY;
    }
    const int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (isIrrelevant(*it, window, desktop)) {
            continue;
        }
        const int y = bottomEdge ? (*it)->frameGeometry().top() - 1 : (*it)->frameGeometry().bottom() + 1;
        if (y < newY && y > oldY
                && !(window->frameGeometry().left() > (*it)->frameGeometry().right()
                     || window->frameGeometry().right() < (*it)->frameGeometry().left())) {
            newY = y;
        }
    }
    return newY;
}

#endif

} // namespace
