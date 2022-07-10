/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"
#include "geo.h"
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

    if (kwinApp()->get_base().get_outputs().empty()) {
        return;
    }

    if (win->geometry_update.fullscreen) {
        auto area = space_window_area(win->space, FullScreenArea, win);
        win->setFrameGeometry(area);
        return;
    }

    if (win->maximizeMode() != maximize_mode::restore) {
        geometry_updates_blocker block(win);

        win->update_maximized(win->geometry_update.max_mode);
        auto const screenArea = space_window_area(win->space, ScreenArea, win);

        auto geo = pending_frame_geometry(win);
        check_offscreen_position(geo, screenArea);
        win->setFrameGeometry(geo);

        return;
    }

    if (win->control->quicktiling() != quicktiles::none) {
        win->setFrameGeometry(electric_border_maximize_geometry(
            win, pending_frame_geometry(win).center(), win->desktop()));
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
        oldDesktop = win->desktop();
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
        win->space, ScreenArea, pending_frame_geometry(win).center(), win->desktop());

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
    for (auto const& r : restricted_move_area(win->space, win->desktop(), strut_area::top)) {
        auto rect = r & tall_frame_geo;
        if (!rect.isEmpty()) {
            top_max = std::max(top_max, rect.y() + rect.height());
        }
    }
    for (auto const& r : restricted_move_area(win->space, win->desktop(), strut_area::right)) {
        auto rect = r & wide_frame_geo;
        if (!rect.isEmpty()) {
            right_max = std::min(right_max, rect.x());
        }
    }
    for (auto const& r : restricted_move_area(win->space, win->desktop(), strut_area::bottom)) {
        auto rect = r & tall_frame_geo;
        if (!rect.isEmpty()) {
            bottom_max = std::min(bottom_max, rect.y());
        }
    }
    for (auto const& r : restricted_move_area(win->space, win->desktop(), strut_area::left)) {
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

    auto const& outputs = kwinApp()->get_base().get_outputs();

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

}
