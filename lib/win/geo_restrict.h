/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"
#include "desktop_get.h"
#include "geo.h"
#include "geo_block.h"
#include "geo_move.h"
#include "window_area.h"

namespace KWin::win
{

inline void check_offscreen_position(QRect& frame_geo, const QRect& screenArea)
{
    if (frame_geo.left() > screenArea.right()) {
        frame_geo.moveLeft(screenArea.right() - screenArea.width() / 4);
    } else if (frame_geo.right() < screenArea.left()) {
        frame_geo.moveRight(screenArea.left() + screenArea.width() / 4);
    }
    if (frame_geo.top() > screenArea.bottom()) {
        frame_geo.moveTop(screenArea.bottom() - screenArea.height() / 4);
    } else if (frame_geo.bottom() < screenArea.top()) {
        frame_geo.moveBottom(screenArea.top() + screenArea.height() / 4);
    }
}

template<typename Win>
void check_workspace_position(Win* win,
                              QRect old_frame_geo = QRect(),
                              int oldDesktop = -2,
                              QRect old_client_geo = QRect())
{
    assert(win->control);

    if (is_desktop(win)) {
        return;
    }
    if (is_dock(win)) {
        return;
    }
    if (is_notification(win) || is_on_screen_display(win)) {
        return;
    }

    if (win->space.base.outputs.empty()) {
        return;
    }

    if (win->geo.update.fullscreen) {
        auto area = space_window_area(win->space, FullScreenArea, win);
        win->setFrameGeometry(area);
        return;
    }

    if (win->maximizeMode() != maximize_mode::restore) {
        geometry_updates_blocker block(win);

        win->update_maximized(win->geo.update.max_mode);
        auto const screenArea = space_window_area(win->space, ScreenArea, win);

        auto geo = pending_frame_geometry(win);
        check_offscreen_position(geo, screenArea);
        win->setFrameGeometry(geo);

        return;
    }

    if (win->control->quicktiling != quicktiles::none) {
        win->setFrameGeometry(electric_border_maximize_geometry(
            win, pending_frame_geometry(win).center(), get_desktop(*win)));
        return;
    }

    enum {
        Left = 0,
        Top,
        Right,
        Bottom,
    };
    int const border[4] = {
        left_border(win),
        top_border(win),
        right_border(win),
        bottom_border(win),
    };

    if (!old_frame_geo.isValid()) {
        old_frame_geo = pending_frame_geometry(win);
    }
    if (oldDesktop == -2) {
        oldDesktop = get_desktop(*win);
    }
    if (!old_client_geo.isValid()) {
        old_client_geo
            = old_frame_geo.adjusted(border[Left], border[Top], -border[Right], -border[Bottom]);
    }

    // If the window was touching an edge before but not now move it so it is again.
    // Old and new maximums have different starting values so windows on the screen
    // edge will move when a new strut is placed on the edge.
    QRect old_screen_area;
    if (in_update_window_area(win->space)) {
        // we need to find the screen area as it was before the change
        old_screen_area
            = QRect(0, 0, win->space.olddisplaysize.width(), win->space.olddisplaysize.height());
        int distance = INT_MAX;
        for (auto const& r : win->space.oldscreensizes) {
            int d = r.contains(old_frame_geo.center())
                ? 0
                : (r.center() - old_frame_geo.center()).manhattanLength();
            if (d < distance) {
                distance = d;
                old_screen_area = r;
            }
        }
    } else {
        old_screen_area
            = space_window_area(win->space, ScreenArea, old_frame_geo.center(), oldDesktop);
    }

    // With full screen height.
    auto const old_tall_frame_geo = QRect(
        old_frame_geo.x(), old_screen_area.y(), old_frame_geo.width(), old_screen_area.height());

    // With full screen width.
    auto const old_wide_frame_geo = QRect(
        old_screen_area.x(), old_frame_geo.y(), old_screen_area.width(), old_frame_geo.height());

    auto old_top_max = old_screen_area.y();
    auto old_right_max = old_screen_area.x() + old_screen_area.width();
    auto old_bottom_max = old_screen_area.y() + old_screen_area.height();
    auto old_left_max = old_screen_area.x();

    auto const screenArea = space_window_area(
        win->space, ScreenArea, pending_frame_geometry(win).center(), get_desktop(*win));

    auto top_max = screenArea.y();
    auto right_max = screenArea.x() + screenArea.width();
    auto bottom_max = screenArea.y() + screenArea.height();
    auto left_max = screenArea.x();

    auto frame_geo = pending_frame_geometry(win);
    auto client_geo
        = frame_geo.adjusted(border[Left], border[Top], -border[Right], -border[Bottom]);

    // Full screen height
    auto const tall_frame_geo
        = QRect(frame_geo.x(), screenArea.y(), frame_geo.width(), screenArea.height());

    // Full screen width
    auto const wide_frame_geo
        = QRect(screenArea.x(), frame_geo.y(), screenArea.width(), frame_geo.height());

    // Get the max strut point for each side where the window is (E.g. Highest point for
    // the bottom struts bounded by the window's left and right sides).

    // Default is to use restrictedMoveArea. That's on active desktop or screen change.
    auto move_area_func = win::restricted_move_area<decltype(win->space)>;
    if (in_update_window_area(win->space)) {
        // On restriected area changes.
        // TODO(romangg): This check back on in_update_window_area and then setting here internally
        //                a different function is bad design. Replace with an argument or something.
        move_area_func = win::previous_restricted_move_area<decltype(win->space)>;
    }

    // These 4 compute old bounds.
    for (auto const& r : move_area_func(win->space, oldDesktop, strut_area::top)) {
        auto rect = r & old_tall_frame_geo;
        if (!rect.isEmpty()) {
            old_top_max = std::max(old_top_max, rect.y() + rect.height());
        }
    }
    for (auto const& r : move_area_func(win->space, oldDesktop, strut_area::right)) {
        auto rect = r & old_wide_frame_geo;
        if (!rect.isEmpty()) {
            old_right_max = std::min(old_right_max, rect.x());
        }
    }
    for (auto const& r : move_area_func(win->space, oldDesktop, strut_area::bottom)) {
        auto rect = r & old_tall_frame_geo;
        if (!rect.isEmpty()) {
            old_bottom_max = std::min(old_bottom_max, rect.y());
        }
    }
    for (auto const& r : move_area_func(win->space, oldDesktop, strut_area::left)) {
        auto rect = r & old_wide_frame_geo;
        if (!rect.isEmpty()) {
            old_left_max = std::max(old_left_max, rect.x() + rect.width());
        }
    }

    // These 4 compute new bounds.
    for (auto const& r : restricted_move_area(win->space, get_desktop(*win), strut_area::top)) {
        auto rect = r & tall_frame_geo;
        if (!rect.isEmpty()) {
            top_max = std::max(top_max, rect.y() + rect.height());
        }
    }
    for (auto const& r : restricted_move_area(win->space, get_desktop(*win), strut_area::right)) {
        auto rect = r & wide_frame_geo;
        if (!rect.isEmpty()) {
            right_max = std::min(right_max, rect.x());
        }
    }
    for (auto const& r : restricted_move_area(win->space, get_desktop(*win), strut_area::bottom)) {
        auto rect = r & tall_frame_geo;
        if (!rect.isEmpty()) {
            bottom_max = std::min(bottom_max, rect.y());
        }
    }
    for (auto const& r : restricted_move_area(win->space, get_desktop(*win), strut_area::left)) {
        auto rect = r & wide_frame_geo;
        if (!rect.isEmpty()) {
            left_max = std::max(left_max, rect.x() + rect.width());
        }
    }

    // Check if the sides were inside or touching but are no longer
    bool keep[4] = {false, false, false, false};
    bool save[4] = {false, false, false, false};
    int padding[4] = {0, 0, 0, 0};

    if (old_frame_geo.x() >= old_left_max) {
        save[Left] = frame_geo.x() < left_max;
    }

    if (old_frame_geo.x() == old_left_max) {
        keep[Left] = frame_geo.x() != left_max;
    } else if (old_client_geo.x() == old_left_max && client_geo.x() != left_max) {
        padding[0] = border[Left];
        keep[Left] = true;
    }

    if (old_frame_geo.y() >= old_top_max) {
        save[Top] = frame_geo.y() < top_max;
    }

    if (old_frame_geo.y() == old_top_max) {
        keep[Top] = frame_geo.y() != top_max;
    } else if (old_client_geo.y() == old_top_max && client_geo.y() != top_max) {
        padding[1] = border[Left];
        keep[Top] = true;
    }

    if (old_frame_geo.right() <= old_right_max - 1) {
        save[Right] = frame_geo.right() > right_max - 1;
    }

    if (old_frame_geo.right() == old_right_max - 1) {
        keep[Right] = frame_geo.right() != right_max - 1;
    } else if (old_client_geo.right() == old_right_max - 1 && client_geo.right() != right_max - 1) {
        padding[2] = border[Right];
        keep[Right] = true;
    }

    if (old_frame_geo.bottom() <= old_bottom_max - 1) {
        save[Bottom] = frame_geo.bottom() > bottom_max - 1;
    }

    if (old_frame_geo.bottom() == old_bottom_max - 1) {
        keep[Bottom] = frame_geo.bottom() != bottom_max - 1;
    } else if (old_client_geo.bottom() == old_bottom_max - 1
               && client_geo.bottom() != bottom_max - 1) {
        padding[3] = border[Bottom];
        keep[Bottom] = true;
    }

    // if randomly touches opposing edges, do not favor either
    if (keep[Left] && keep[Right]) {
        keep[Left] = keep[Right] = false;
        padding[0] = padding[2] = 0;
    }
    if (keep[Top] && keep[Bottom]) {
        keep[Top] = keep[Bottom] = false;
        padding[1] = padding[3] = 0;
    }

    auto const& outputs = win->space.base.outputs;

    if (save[Left] || keep[Left]) {
        frame_geo.moveLeft(std::max(left_max, screenArea.x()) - padding[0]);
    }
    if (padding[0] && base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
        frame_geo.moveLeft(frame_geo.left() + padding[0]);
    }
    if (save[Top] || keep[Top]) {
        frame_geo.moveTop(std::max(top_max, screenArea.y()) - padding[1]);
    }
    if (padding[1] && base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
        frame_geo.moveTop(frame_geo.top() + padding[1]);
    }
    if (save[Right] || keep[Right]) {
        frame_geo.moveRight(std::min(right_max - 1, screenArea.right()) + padding[2]);
    }
    if (padding[2] && base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
        frame_geo.moveRight(frame_geo.right() - padding[2]);
    }
    if (old_frame_geo.x() >= old_left_max && frame_geo.x() < left_max) {
        frame_geo.setLeft(std::max(left_max, screenArea.x()));
    } else if (old_client_geo.x() >= old_left_max && frame_geo.x() + border[Left] < left_max) {
        frame_geo.setLeft(std::max(left_max, screenArea.x()) - border[Left]);
        if (base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
            frame_geo.setLeft(frame_geo.left() + border[Left]);
        }
    }
    if (save[Bottom] || keep[Bottom]) {
        frame_geo.moveBottom(std::min(bottom_max - 1, screenArea.bottom()) + padding[3]);
    }
    if (padding[3] && base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
        frame_geo.moveBottom(frame_geo.bottom() - padding[3]);
    }

    if (old_frame_geo.y() >= old_top_max && frame_geo.y() < top_max) {
        frame_geo.setTop(std::max(top_max, screenArea.y()));
    } else if (old_client_geo.y() >= old_top_max && frame_geo.y() + border[Top] < top_max) {
        frame_geo.setTop(std::max(top_max, screenArea.y()) - border[Top]);
        if (base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
            frame_geo.setTop(frame_geo.top() + border[Top]);
        }
    }

    check_offscreen_position(frame_geo, screenArea);

    // Obey size hints. TODO: We really should make sure it stays in the right place
    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::any));

    win->setFrameGeometry(frame_geo);
}

/**
 * Client \a c is moved around to position \a pos. This gives the
 * space:: the opportunity to interveniate and to implement
 * snap-to-windows functionality.
 *
 * The parameter \a snapAdjust is a multiplier used to calculate the
 * effective snap zones. When 1.0, it means that the snap zones will be
 * used without change.
 */
template<typename Space, typename Win>
QPoint adjust_window_position(Space const& space,
                              Win const& window,
                              QPoint pos,
                              bool unrestricted,
                              double snapAdjust = 1.0)
{
    QSize borderSnapZone(space.options->qobject->borderSnapZone(),
                         space.options->qobject->borderSnapZone());
    QRect maxRect;
    auto guideMaximized = maximize_mode::restore;

    if (window.maximizeMode() != maximize_mode::restore) {
        maxRect = space_window_area(
            space, MaximizeArea, pos + QRect({}, window.geo.size()).center(), get_desktop(window));
        auto geo = window.geo.frame;
        if (flags(window.maximizeMode() & maximize_mode::horizontal)
            && (geo.x() == maxRect.left() || geo.right() == maxRect.right())) {
            guideMaximized |= maximize_mode::horizontal;
            borderSnapZone.setWidth(qMax(borderSnapZone.width() + 2, maxRect.width() / 16));
        }
        if (flags(window.maximizeMode() & maximize_mode::vertical)
            && (geo.y() == maxRect.top() || geo.bottom() == maxRect.bottom())) {
            guideMaximized |= maximize_mode::vertical;
            borderSnapZone.setHeight(qMax(borderSnapZone.height() + 2, maxRect.height() / 16));
        }
    }

    if (space.options->qobject->windowSnapZone() || !borderSnapZone.isNull()
        || space.options->qobject->centerSnapZone()) {
        auto const& outputs = space.base.outputs;
        const bool sOWO = space.options->qobject->isSnapOnlyWhenOverlapping();
        auto output
            = base::get_nearest_output(outputs, pos + QRect({}, window.geo.size()).center());

        if (maxRect.isNull()) {
            maxRect = space_window_area(space, MovementArea, output, get_desktop(window));
        }

        const int xmin = maxRect.left();
        const int xmax = maxRect.right() + 1; // desk size
        const int ymin = maxRect.top();
        const int ymax = maxRect.bottom() + 1;

        const int cx(pos.x());
        const int cy(pos.y());
        const int cw(window.geo.size().width());
        const int ch(window.geo.size().height());
        const int rx(cx + cw);
        const int ry(cy + ch); // these don't change

        int nx(cx), ny(cy); // buffers
        int deltaX(xmax);
        int deltaY(ymax); // minimum distance to other clients

        int lx, ly, lrx, lry; // coords and size for the comparison client, l

        // border snap
        const int snapX = borderSnapZone.width() * snapAdjust; // snap trigger
        const int snapY = borderSnapZone.height() * snapAdjust;
        if (snapX || snapY) {
            auto geo = window.geo.frame;
            auto frameMargins = frame_margins(&window);

            // snap to titlebar / snap to window borders on inner screen edges
            if (frameMargins.left()
                && (flags(window.maximizeMode() & maximize_mode::horizontal)
                    || base::get_intersecting_outputs(
                           outputs,
                           geo.translated(maxRect.x() - (frameMargins.left() + geo.x()), 0))
                            .size()
                        > 1)) {
                frameMargins.setLeft(0);
            }
            if (frameMargins.right()
                && (flags(window.maximizeMode() & maximize_mode::horizontal)
                    || base::get_intersecting_outputs(
                           outputs,
                           geo.translated(maxRect.right() + frameMargins.right() - geo.right(), 0))
                            .size()
                        > 1)) {
                frameMargins.setRight(0);
            }
            if (frameMargins.top()) {
                frameMargins.setTop(0);
            }
            if (frameMargins.bottom()
                && (flags(window.maximizeMode() & maximize_mode::vertical)
                    || base::get_intersecting_outputs(
                           outputs,
                           geo.translated(0,
                                          maxRect.bottom() + frameMargins.bottom() - geo.bottom()))
                            .size()
                        > 1)) {
                frameMargins.setBottom(0);
            }
            if ((sOWO ? (cx < xmin) : true) && (qAbs(xmin - cx) < snapX)) {
                deltaX = xmin - cx;
                nx = xmin - frameMargins.left();
            }
            if ((sOWO ? (rx > xmax) : true) && (qAbs(rx - xmax) < snapX)
                && (qAbs(xmax - rx) < deltaX)) {
                deltaX = rx - xmax;
                nx = xmax - cw + frameMargins.right();
            }

            if ((sOWO ? (cy < ymin) : true) && (qAbs(ymin - cy) < snapY)) {
                deltaY = ymin - cy;
                ny = ymin - frameMargins.top();
            }
            if ((sOWO ? (ry > ymax) : true) && (qAbs(ry - ymax) < snapY)
                && (qAbs(ymax - ry) < deltaY)) {
                deltaY = ry - ymax;
                ny = ymax - ch + frameMargins.bottom();
            }
        }

        // windows snap
        int snap = space.options->qobject->windowSnapZone() * snapAdjust;
        if (snap) {
            for (auto win : space.windows) {
                std::visit(overload{[&](auto&& win) {
                               if (!win->control) {
                                   return;
                               }
                               if constexpr (std::is_same_v<std::decay_t<decltype(win)>, Win*>) {
                                   if (win == &window) {
                                       return;
                                   }
                               }
                               if (win->control->minimized) {
                                   return;
                               }
                               if (!win->isShown()) {
                                   return;
                               }
                               if (!on_desktop(win, get_desktop(window))
                                   && !on_desktop(&window, get_desktop(*win))) {
                                   // wrong virtual desktop
                                   return;
                               }
                               if (is_desktop(win) || is_splash(win) || is_applet_popup(win)) {
                                   return;
                               }

                               lx = win->geo.pos().x();
                               ly = win->geo.pos().y();
                               lrx = lx + win->geo.size().width();
                               lry = ly + win->geo.size().height();
                           }},
                           win);

                if (!flags(guideMaximized & maximize_mode::horizontal)
                    && (((cy <= lry) && (cy >= ly)) || ((ry >= ly) && (ry <= lry))
                        || ((cy <= ly) && (ry >= lry)))) {
                    if ((sOWO ? (cx < lrx) : true) && (qAbs(lrx - cx) < snap)
                        && (qAbs(lrx - cx) < deltaX)) {
                        deltaX = qAbs(lrx - cx);
                        nx = lrx;
                    }
                    if ((sOWO ? (rx > lx) : true) && (qAbs(rx - lx) < snap)
                        && (qAbs(rx - lx) < deltaX)) {
                        deltaX = qAbs(rx - lx);
                        nx = lx - cw;
                    }
                }

                if (!flags(guideMaximized & maximize_mode::vertical)
                    && (((cx <= lrx) && (cx >= lx)) || ((rx >= lx) && (rx <= lrx))
                        || ((cx <= lx) && (rx >= lrx)))) {
                    if ((sOWO ? (cy < lry) : true) && (qAbs(lry - cy) < snap)
                        && (qAbs(lry - cy) < deltaY)) {
                        deltaY = qAbs(lry - cy);
                        ny = lry;
                    }
                    // if ( (qAbs( ry-ly ) < snap) && (qAbs( ry - ly ) < deltaY ))
                    if ((sOWO ? (ry > ly) : true) && (qAbs(ry - ly) < snap)
                        && (qAbs(ry - ly) < deltaY)) {
                        deltaY = qAbs(ry - ly);
                        ny = ly - ch;
                    }
                }

                // Corner snapping
                if (!flags(guideMaximized & maximize_mode::vertical)
                    && (nx == lrx || nx + cw == lx)) {
                    if ((sOWO ? (ry > lry) : true) && (qAbs(lry - ry) < snap)
                        && (qAbs(lry - ry) < deltaY)) {
                        deltaY = qAbs(lry - ry);
                        ny = lry - ch;
                    }
                    if ((sOWO ? (cy < ly) : true) && (qAbs(cy - ly) < snap)
                        && (qAbs(cy - ly) < deltaY)) {
                        deltaY = qAbs(cy - ly);
                        ny = ly;
                    }
                }
                if (!flags(guideMaximized & maximize_mode::horizontal)
                    && (ny == lry || ny + ch == ly)) {
                    if ((sOWO ? (rx > lrx) : true) && (qAbs(lrx - rx) < snap)
                        && (qAbs(lrx - rx) < deltaX)) {
                        deltaX = qAbs(lrx - rx);
                        nx = lrx - cw;
                    }
                    if ((sOWO ? (cx < lx) : true) && (qAbs(cx - lx) < snap)
                        && (qAbs(cx - lx) < deltaX)) {
                        deltaX = qAbs(cx - lx);
                        nx = lx;
                    }
                }
            }
        }

        // center snap
        snap = space.options->qobject->centerSnapZone() * snapAdjust; // snap trigger
        if (snap) {
            int diffX = qAbs((xmin + xmax) / 2 - (cx + cw / 2));
            int diffY = qAbs((ymin + ymax) / 2 - (cy + ch / 2));
            if (diffX < snap && diffY < snap && diffX < deltaX && diffY < deltaY) {
                // Snap to center of screen
                nx = (xmin + xmax) / 2 - cw / 2;
                ny = (ymin + ymax) / 2 - ch / 2;
            } else if (space.options->qobject->borderSnapZone()) {
                // Enhance border snap
                if ((nx == xmin || nx == xmax - cw) && diffY < snap && diffY < deltaY) {
                    // Snap to vertical center on screen edge
                    ny = (ymin + ymax) / 2 - ch / 2;
                } else if (((unrestricted ? ny == ymin : ny <= ymin) || ny == ymax - ch)
                           && diffX < snap && diffX < deltaX) {
                    // Snap to horizontal center on screen edge
                    nx = (xmin + xmax) / 2 - cw / 2;
                }
            }
        }

        pos = QPoint(nx, ny);
    }
    return pos;
}

template<typename Space, typename Win>
QRect adjust_window_size(Space const& space, Win const& window, QRect moveResizeGeom, position mode)
{
    // adapted from adjustClientPosition on 29May2004
    // this function is called when resizing a window and will modify
    // the new dimensions to snap to other windows/borders if appropriate
    if (space.options->qobject->windowSnapZone() || space.options->qobject->borderSnapZone()) {
        // || space.options->centerSnapZone )
        const bool sOWO = space.options->qobject->isSnapOnlyWhenOverlapping();

        auto const maxRect = space_window_area(space,
                                               MovementArea,
                                               QRect(QPoint(0, 0), window.geo.size()).center(),
                                               get_desktop(window));
        const int xmin = maxRect.left();
        const int xmax = maxRect.right(); // desk size
        const int ymin = maxRect.top();
        const int ymax = maxRect.bottom();

        const int cx(moveResizeGeom.left());
        const int cy(moveResizeGeom.top());
        const int rx(moveResizeGeom.right());
        const int ry(moveResizeGeom.bottom());

        int newcx(cx), newcy(cy); // buffers
        int newrx(rx), newry(ry);
        int deltaX(xmax);
        int deltaY(ymax); // minimum distance to other clients

        int lx, ly, lrx, lry; // coords and size for the comparison client, l

        // border snap
        // snap trigger
        int snap = space.options->qobject->borderSnapZone();
        if (snap) {
            deltaX = int(snap);
            deltaY = int(snap);

            auto snap_border_top = [&] {
                if ((sOWO ? (newcy < ymin) : true) && (qAbs(ymin - newcy) < deltaY)) {
                    deltaY = qAbs(ymin - newcy);
                    newcy = ymin;
                }
            };

            auto snap_border_bottom = [&] {
                if ((sOWO ? (newry > ymax) : true) && (qAbs(ymax - newry) < deltaY)) {
                    deltaY = qAbs(ymax - newcy);
                    newry = ymax;
                }
            };

            auto snap_border_left = [&] {
                if ((sOWO ? (newcx < xmin) : true) && (qAbs(xmin - newcx) < deltaX)) {
                    deltaX = qAbs(xmin - newcx);
                    newcx = xmin;
                }
            };

            auto snap_border_right = [&] {
                if ((sOWO ? (newrx > xmax) : true) && (qAbs(xmax - newrx) < deltaX)) {
                    deltaX = qAbs(xmax - newrx);
                    newrx = xmax;
                }
            };

            switch (mode) {
            case position::bottom_right:
                snap_border_bottom();
                snap_border_right();
                break;
            case position::right:
                snap_border_right();
                break;
            case position::bottom:
                snap_border_bottom();
                break;
            case position::top_left:
                snap_border_top();
                snap_border_left();
                break;
            case position::left:
                snap_border_left();
                break;
            case position::top:
                snap_border_top();
                break;
            case position::top_right:
                snap_border_top();
                snap_border_right();
                break;
            case position::bottom_left:
                snap_border_bottom();
                snap_border_left();
                break;
            default:
                abort();
                break;
            }
        }

        // windows snap
        snap = space.options->qobject->windowSnapZone();
        if (snap) {
            deltaX = int(snap);
            deltaY = int(snap);
            for (auto win : space.windows) {
                std::visit(
                    overload{[&](auto&& win) {
                        if (!win->control
                            || !on_desktop(win, space.virtual_desktop_manager->current())
                            || win->control->minimized) {
                            return;
                        }
                        if constexpr (std::is_same_v<std::remove_pointer_t<decltype(win)>, Win>) {
                            if (win == &window) {
                                return;
                            }
                        }
                        lx = win->geo.pos().x() - 1;
                        ly = win->geo.pos().y() - 1;
                        lrx = win->geo.pos().x() + win->geo.size().width();
                        lry = win->geo.pos().y() + win->geo.size().height();

                        auto within_height = [&] {
                            return ((newcy <= lry) && (newcy >= ly))
                                || ((newry >= ly) && (newry <= lry))
                                || ((newcy <= ly) && (newry >= lry));
                        };
                        auto within_width = [&] {
                            return ((cx <= lrx) && (cx >= lx)) || ((rx >= lx) && (rx <= lrx))
                                || ((cx <= lx) && (rx >= lrx));
                        };

                        auto snap_window_top = [&] {
                            if ((sOWO ? (newcy < lry) : true) && within_width()
                                && (qAbs(lry - newcy) < deltaY)) {
                                deltaY = qAbs(lry - newcy);
                                newcy = lry;
                            }
                        };
                        auto snap_window_bottom = [&] {
                            if ((sOWO ? (newry > ly) : true) && within_width()
                                && (qAbs(ly - newry) < deltaY)) {
                                deltaY = qAbs(ly - newry);
                                newry = ly;
                            }
                        };
                        auto snap_window_left = [&] {
                            if ((sOWO ? (newcx < lrx) : true) && within_height()
                                && (qAbs(lrx - newcx) < deltaX)) {
                                deltaX = qAbs(lrx - newcx);
                                newcx = lrx;
                            }
                        };
                        auto snap_window_right = [&] {
                            if ((sOWO ? (newrx > lx) : true) && within_height()
                                && (qAbs(lx - newrx) < deltaX)) {
                                deltaX = qAbs(lx - newrx);
                                newrx = lx;
                            }
                        };
                        auto snap_window_c_top = [&] {
                            if ((sOWO ? (newcy < ly) : true) && (newcx == lrx || newrx == lx)
                                && qAbs(ly - newcy) < deltaY) {
                                deltaY = qAbs(ly - newcy + 1);
                                newcy = ly + 1;
                            }
                        };
                        auto snap_window_c_bottom = [&] {
                            if ((sOWO ? (newry > lry) : true) && (newcx == lrx || newrx == lx)
                                && qAbs(lry - newry) < deltaY) {
                                deltaY = qAbs(lry - newry - 1);
                                newry = lry - 1;
                            }
                        };
                        auto snap_window_c_left = [&] {
                            if ((sOWO ? (newcx < lx) : true) && (newcy == lry || newry == ly)
                                && qAbs(lx - newcx) < deltaX) {
                                deltaX = qAbs(lx - newcx + 1);
                                newcx = lx + 1;
                            }
                        };
                        auto snap_window_c_right = [&] {
                            if ((sOWO ? (newrx > lrx) : true) && (newcy == lry || newry == ly)
                                && qAbs(lrx - newrx) < deltaX) {
                                deltaX = qAbs(lrx - newrx - 1);
                                newrx = lrx - 1;
                            }
                        };

                        switch (mode) {
                        case position::bottom_right:
                            snap_window_bottom();
                            snap_window_right();
                            snap_window_c_bottom();
                            snap_window_c_right();
                            break;
                        case position::right:
                            snap_window_right();
                            snap_window_c_right();
                            break;
                        case position::bottom:
                            snap_window_bottom();
                            snap_window_c_bottom();
                            break;
                        case position::top_left:
                            snap_window_top();
                            snap_window_left();
                            snap_window_c_top();
                            snap_window_c_left();
                            break;
                        case position::left:
                            snap_window_left();
                            snap_window_c_left();
                            break;
                        case position::top:
                            snap_window_top();
                            snap_window_c_top();
                            break;
                        case position::top_right:
                            snap_window_top();
                            snap_window_right();
                            snap_window_c_top();
                            snap_window_c_right();
                            break;
                        case position::bottom_left:
                            snap_window_bottom();
                            snap_window_left();
                            snap_window_c_bottom();
                            snap_window_c_left();
                            break;
                        default:
                            abort();
                            break;
                        }
                    }},
                    win);
            }
        }

        // center snap
        // snap = space.options->centerSnapZone;
        // if (snap)
        //    {
        //    // Don't resize snap to center as it interferes too much
        //    // There are two ways of implementing this if wanted:
        //    // 1) Snap only to the same points that the move snap does, and
        //    // 2) Snap to the horizontal and vertical center lines of the screen
        //    }

        moveResizeGeom = QRect(QPoint(newcx, newcy), QPoint(newrx, newry));
    }

    return moveResizeGeom;
}

}
